import {contextBridge,ipcRenderer} from 'electron';
import type {DesktopAPI,LogEntry,RuntimeState} from '../shared/types';
const subscribe=<T>(channel:string,callback:(value:T)=>void)=>{const handler=(_event:unknown,value:T)=>callback(value);ipcRenderer.on(channel,handler);return()=>ipcRenderer.removeListener(channel,handler);};
const api:DesktopAPI={
  load:()=>ipcRenderer.invoke('app:load'),
  saveWorkspace:value=>ipcRenderer.invoke('workspace:save',value),
  savePreferences:value=>ipcRenderer.invoke('preferences:save',value),
  send:id=>ipcRenderer.invoke('request:send',id),
  start:id=>ipcRenderer.invoke('request:start',id),
  stop:id=>ipcRenderer.invoke('request:stop',id),
  clearHistory:id=>ipcRenderer.invoke('history:clear',id),
  listPorts:()=>ipcRenderer.invoke('ports:list'),
  importWorkspace:()=>ipcRenderer.invoke('workspace:import'),
  exportWorkspace:()=>ipcRenderer.invoke('workspace:export'),
  openDataDirectory:()=>ipcRenderer.invoke('directory:open'),
  windowAction:action=>ipcRenderer.send('window:action',action),
  onLog:callback=>subscribe<LogEntry>('log',callback),
  onRuntime:callback=>subscribe<RuntimeState>('runtime',callback),
  onError:callback=>subscribe<string>('app:error',callback),
  onBeforeClose:callback=>{const handler=async()=>{try{await callback();ipcRenderer.send('window:close-ready',true);}catch(error){ipcRenderer.send('window:close-ready',false,String(error));}};ipcRenderer.on('window:before-close',handler);return()=>ipcRenderer.removeListener('window:before-close',handler);}
};
contextBridge.exposeInMainWorld('desktop',api);
