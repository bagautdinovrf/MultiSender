import {app,BrowserWindow,dialog,ipcMain,Menu,nativeImage,shell,screen} from 'electron';
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {SerialPort} from 'serialport';
import {Storage} from './storage';
import {RequestEngine,validateConfig} from './engine';
import {findRequest,resolveRequest} from '../shared/model';
import type {ResolvedRequest} from '../shared/types';

app.setName('MultiSender Electron');
// This override makes verification isolated; normal builds remain portable.
const override=process.env.MULTISENDER_DATA_DIR;
if(override)app.setPath('userData',path.join(path.resolve(override),'runtime'));
else app.setPath('userData',path.join(app.getPath('appData'),'MultiSenderElectron'));
const gotLock=app.requestSingleInstanceLock();
if(!gotLock)app.quit();
let window:BrowserWindow|null=null;
let storage:Storage;
let engine:RequestEngine;
let allowClose=false;
let closing=false;
let closeTimer:ReturnType<typeof setTimeout>|undefined;
const root=path.resolve(__dirname,'../../..');
const rendererFile=path.resolve(__dirname,'../renderer/index.html');
const send=(channel:string,payload:unknown)=>{if(window&&!window.isDestroyed())window.webContents.send(channel,payload);};
const userError=(message:string)=>send('app:error',message);
const versionFile=app.isPackaged?path.join(process.resourcesPath,'VERSION'):path.join(root,'VERSION');
const version=()=>fs.readFileSync(versionFile,'utf8').trim();
function trusted(event:Electron.IpcMainInvokeEvent|Electron.IpcMainEvent){if(!window||event.sender!==window.webContents||event.senderFrame!==window.webContents.mainFrame||event.senderFrame?.url!==pathToFileURL(rendererFile).href)throw new Error('Недопустимый источник команды.');}
function handle(channel:string,callback:(...args:any[])=>unknown){ipcMain.handle(channel,(event,...args)=>{trusted(event);return callback(...args);});}
function request(id:unknown){if(typeof id!=='string')throw new Error('Не указан запрос.');const config=findRequest(storage.workspace,id);if(!config)throw new Error('Запрос не найден.');return config;}
function validate(config:ResolvedRequest){const error=validateConfig(config);if(error)throw new Error(error);}
function stop(id?:string){const c=storage.workspace.collections.find(c=>c.id===id);if(c)c.requests.forEach(r=>engine.stop(r.id));else engine.stop(id);}
function wireIPC(){
  handle('app:load',()=>({workspace:storage.workspace,history:storage.history,preferences:storage.preferences,runtime:engine.runtime(),version:version(),dataDirectory:storage.directory,importedFrom:storage.importedFrom,warnings:storage.warnings}));
  handle('workspace:save',(workspace:unknown)=>{
    const oldIds=storage.workspace.collections.flatMap(c=>c.requests.map(r=>r.id));
    storage.saveWorkspace(workspace);
    const ids=new Set(storage.workspace.collections.flatMap(c=>c.requests.map(r=>r.id)));
    oldIds.filter(id=>!ids.has(id)).forEach(id=>engine.stop(id));
  });
  handle('preferences:save',(preferences:unknown)=>storage.savePreferences(preferences));
  handle('request:send',(id:string)=>{const config=request(id);validate(config);engine.send(config);});
  handle('request:start',(id:string)=>{
    const c=storage.workspace.collections.find(c=>c.id===id);const configs=c?c.requests.map(r=>resolveRequest(c,r)):[request(id)];
    if(!configs.length)throw new Error('Добавьте запрос в коллекцию.');
    configs.forEach(validate);configs.forEach(config=>engine.start(config));
  });
  handle('request:stop',(id?:string)=>{if(id!==undefined&&typeof id!=='string')throw new Error('Некорректный идентификатор.');stop(id);});
  handle('history:clear',(id?:string)=>storage.clearHistory(id));
  handle('ports:list',()=>SerialPort.list());
  handle('workspace:import',async()=>{
    const result=await dialog.showOpenDialog(window!,{title:'Импорт коллекций MultiSender',filters:[{name:'Рабочее пространство JSON',extensions:['json']}],properties:['openFile']});
    return result.canceled?null:storage.importFile(result.filePaths[0]);
  });
  handle('workspace:export',async()=>{
    const result=await dialog.showSaveDialog(window!,{title:'Экспорт коллекций',defaultPath:'MultiSender.workspace.json',filters:[{name:'JSON',extensions:['json']}]});
    if(result.canceled||!result.filePath)return false;
    // Export must never overwrite data belonging to the preserved original.
    const canonical=(file:string)=>(fs.existsSync(file)?fs.realpathSync(file):path.join(fs.realpathSync(path.dirname(file)),path.basename(file))).toLowerCase();
    const target=canonical(result.filePath);
    const legacyDirs=[path.resolve(root,'build_agent/package'),path.resolve(storage.directory,'../../build_agent/package')];
    if(legacyDirs.some(dir=>fs.existsSync(dir)&&target.startsWith(canonical(dir)+path.sep))||/^(multisender|httprequester|multisenderqml)\.(workspace|history)\.json$/i.test(path.basename(result.filePath))&&fs.existsSync(result.filePath))throw new Error('Для экспорта выберите новый файл вне исходной Qt-программы.');
    storage.exportFile(target);return true;
  });
  handle('directory:open',async()=>{const error=await shell.openPath(storage.directory);if(error)throw new Error(error);});
  ipcMain.on('window:action',(event,action)=>{trusted(event);if(action==='minimize')window?.minimize();else if(action==='maximize'){if(window?.isMaximized())window.unmaximize();else window?.maximize();}else if(action==='close')window?.close();});
  ipcMain.on('window:close-ready',(event,success:boolean,message?:string)=>{trusted(event);if(!closing)return;if(closeTimer)clearTimeout(closeTimer);if(success){allowClose=true;window?.close();}else{closing=false;dialog.showErrorBox('Не удалось сохранить изменения',message||'Исправьте ошибку сохранения и повторите закрытие.');}});
}
async function createWindow(){
  const directory=override?path.resolve(override):app.isPackaged?path.dirname(app.getPath('exe')):path.join(root,'electron/data');
  const legacyDirs=override?[]:[path.resolve(root,'build_agent/package'),directory,path.resolve(directory,'../../build_agent/package')];
  storage=new Storage(directory,[...new Set(legacyDirs)],userError);storage.load();
  engine=new RequestEngine(entry=>{storage.addLog(entry);send('log',entry);},state=>send('runtime',state),`MultiSenderElectron/${version()}`);
  engine.restoreSequence(storage.history);
  wireIPC();Menu.setApplicationMenu(null);
  const saved=storage.preferences.window;
  const onScreen=saved?.x!==undefined&&saved?.y!==undefined&&screen.getAllDisplays().some(d=>saved.x!<d.workArea.x+d.workArea.width-100&&saved.x!+saved.width>d.workArea.x+100&&saved.y!<d.workArea.y+d.workArea.height-100&&saved.y!+saved.height>d.workArea.y+100);
  const iconFile=app.isPackaged?path.join(process.resourcesPath,'icon.png'):path.join(root,'electron/assets/icon.png');
  window=new BrowserWindow({width:saved?.width??1440,height:saved?.height??960,...(onScreen?{x:saved?.x,y:saved?.y}:{}),minWidth:1000,minHeight:680,title:`MultiSender Electron`,frame:false,backgroundColor:'#111315',show:false,icon:fs.existsSync(iconFile)?nativeImage.createFromPath(iconFile):undefined,webPreferences:{preload:path.join(__dirname,'preload.cjs'),contextIsolation:true,nodeIntegration:false,sandbox:true,spellcheck:false,webSecurity:true}});
  window.webContents.setWindowOpenHandler(()=>({action:'deny'}));
  window.webContents.on('will-navigate',event=>event.preventDefault());
  window.webContents.session.setPermissionRequestHandler((_contents,_permission,callback)=>callback(false));
  window.webContents.session.setPermissionCheckHandler(()=>false);
  window.once('ready-to-show',()=>{if(saved?.maximized)window?.maximize();window?.show();});
  window.on('close',event=>{
    if(!allowClose){event.preventDefault();if(!closing){closing=true;send('window:before-close',null);closeTimer=setTimeout(async()=>{closing=false;const result=await dialog.showMessageBox(window!,{type:'warning',message:'Интерфейс не ответил на запрос сохранения.',detail:'Можно остаться в приложении или закрыть его без последних несохранённых правок.',buttons:['Остаться','Закрыть'],defaultId:0,cancelId:0});if(result.response===1){allowClose=true;window?.close();}},5000);}return;}
    engine.stop();
    try{storage.saveWindow({...window!.getNormalBounds(),maximized:window!.isMaximized()});storage.flush();}catch(error){dialog.showErrorBox('Ошибка сохранения',String(error));event.preventDefault();allowClose=false;closing=false;}
  });
  window.on('closed',()=>{window=null;});
  await window.loadFile(rendererFile);
}
app.on('second-instance',()=>{if(window?.isMinimized())window.restore();window?.focus();});
app.on('window-all-closed',()=>app.quit());
if(gotLock)app.whenReady().then(createWindow).catch(error=>{dialog.showErrorBox('MultiSender Electron — ошибка запуска',String(error));app.quit();});
