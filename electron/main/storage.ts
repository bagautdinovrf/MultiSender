import fs from 'node:fs';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { defaultPreferences, defaultWorkspace, newCollection, newRequest } from '../shared/model';
import type { Collection, Header, LogEntry, Preferences, RequestDefinition, Workspace } from '../shared/types';

const record = (v: unknown): Record<string, any> => v !== null && typeof v === 'object' && !Array.isArray(v) ? v as Record<string, any> : {};
const str = (v: unknown, fallback = '') => typeof v === 'string' ? v : fallback;
const num = (v: unknown, fallback: number) => typeof v === 'number' && Number.isFinite(v) ? v : fallback;
const bool = (v: unknown) => v === true;
function headers(v: unknown): Header[] { return Array.isArray(v) ? v.slice(0,1000).map(item=>{const h=record(item);return {name:str(h.name),value:str(h.value),enabled:h.enabled!==false};}) : []; }

export function normalizeWorkspace(value: unknown): Workspace {
  const root=record(value);
  if (!Array.isArray(root.collections)) throw new Error('Файл не содержит коллекций MultiSender.');
  if (root.collections.length>10000) throw new Error('Слишком много коллекций в файле.');
  if (root.schemaVersion !== undefined && root.schemaVersion !== 1) throw new Error('Эта версия формата коллекций пока не поддерживается.');
  const ids=new Set<string>();
  const unique=(v:unknown) => { let id=str(v).replace(/^\{|\}$/g,''); if (!id || ids.has(id)) id=randomUUID(); ids.add(id);return id; };
  const collections:Collection[]=root.collections.map((item:unknown)=>{
    const c=record(item), defaults=newCollection();
    if (!Array.isArray(c.requests)) throw new Error('В коллекции отсутствует список запросов.');
    if(c.requests.length>10000)throw new Error('Слишком много запросов в коллекции.');
    return {...defaults,id:unique(c.id),name:str(c.name,defaults.name),host:str(c.host,defaults.host),port:num(c.port,defaults.port),intervalMs:num(c.intervalMs,defaults.intervalMs),maxInFlight:num(c.maxInFlight,defaults.maxInFlight),requests:c.requests.map((item:unknown):RequestDefinition=>{
      const r=record(item), d=newRequest(), s=record(r.serial);
      const protocol=str(r.protocol,'HTTP').toUpperCase();
      if(!['HTTP','TCP','UDP','COM'].includes(protocol))throw new Error(`Неизвестный протокол: ${protocol}`);
      return {...d,id:unique(r.id),name:str(r.name,d.name),protocol:protocol as RequestDefinition['protocol'],method:r.method==='POST'?'POST':'GET',target:str(r.target,d.target),body:str(r.body),waitForResponse:bool(r.waitForResponse),timeoutMs:num(r.timeoutMs,d.timeoutMs),host:str(r.host,d.host),port:num(r.port,d.port),intervalMs:num(r.intervalMs,d.intervalMs),maxInFlight:num(r.maxInFlight,d.maxInFlight),overrideHost:bool(r.overrideHost),overridePort:bool(r.overridePort),overrideIntervalMs:bool(r.overrideIntervalMs),overrideMaxInFlight:bool(r.overrideMaxInFlight),headers:headers(r.headers),serial:{portName:str(s.portName,d.serial.portName),baudRate:num(s.baudRate,9600),dataBits:num(s.dataBits,8) as 5|6|7|8,parity:num(s.parity,0),stopBits:num(s.stopBits,1),flowControl:num(s.flowControl,0)}};
    })};
  });
  return {schemaVersion:1,collections};
}
export function normalizePreferences(value: unknown): Preferences {
  if(value===null||typeof value!=='object'||Array.isArray(value)||!Object.keys(value).some(key=>['tabs','activeTab','theme','sidebarWidth','editorHeight','detailHeight','prettyJson','window'].includes(key)))throw new Error('Файл настроек имеет неверную структуру. Исходный файл сохранён.');
  const v=record(value),d=defaultPreferences(),w=record(v.window);
  const clamp=(n:unknown,fallback:number,min:number,max:number)=>Math.min(max,Math.max(min,num(n,fallback)));
  return {tabs:Array.isArray(v.tabs)?v.tabs.filter((x:unknown)=>typeof x==='string').slice(0,200):[],activeTab:str(v.activeTab),theme:v.theme==='light'?'light':'dark',sidebarWidth:clamp(v.sidebarWidth,d.sidebarWidth,210,480),editorHeight:clamp(v.editorHeight,d.editorHeight,180,850),detailHeight:clamp(v.detailHeight,d.detailHeight,120,650),prettyJson:v.prettyJson!==false,window:Object.keys(w).length?{width:clamp(w.width,1440,1000,3840),height:clamp(w.height,960,680,2160),x:typeof w.x==='number'?w.x:undefined,y:typeof w.y==='number'?w.y:undefined,maximized:bool(w.maximized)}:undefined};
}
function normalizeHistory(value:unknown):LogEntry[] {
  const root=record(value);
  if(!root.requests||typeof root.requests!=='object'||Array.isArray(root.requests))throw new Error('Файл истории не содержит объект requests. Исходный файл сохранён.');
  if(root.schemaVersion!==undefined&&root.schemaVersion!==1)throw new Error('Версия формата истории пока не поддерживается. Исходный файл сохранён.');
  const source=record(root.requests),result:LogEntry[]=[];
  for(const [requestId,rows] of Object.entries(source)) {
    if(!Array.isArray(rows))continue;
    for(const value of rows.slice(-1000)){
      const v=record(value); const protocol=str(v.protocol,'HTTP').toUpperCase();
      const pending=Number(v.result)===0;
      result.push({entryId:str(v.entryId)||randomUUID(),sequence:String(v.sequence??'0'),collectionId:str(v.collectionId),requestId:str(v.requestId,requestId),collectionName:str(v.collectionName),requestName:str(v.requestName),protocol:(['HTTP','TCP','UDP','COM'].includes(protocol)?protocol:'HTTP') as LogEntry['protocol'],method:v.method==='POST'?'POST':'GET',sentAt:str(v.sentAt),completedAt:str(v.completedAt),result:pending?4:([1,2,3,4,5,6].includes(Number(v.result))?Number(v.result):3) as LogEntry['result'],url:str(v.url),requestBody:str(v.requestBody),responseBody:str(v.responseBody),requestHeaders:headers(v.requestHeaders),responseHeaders:headers(v.responseHeaders),errorText:pending?'Операция прервана при закрытии приложения.':str(v.errorText),httpStatus:num(v.httpStatus,0),elapsedMs:Number(v.elapsedMs)||0,responseHex:typeof v.responseHex==='string'?v.responseHex:undefined,responseBytes:typeof v.responseBytes==='number'?v.responseBytes:undefined});
    }
  }
  return result.sort((a,b)=>a.sentAt.localeCompare(b.sentAt));
}
export class Storage {
  workspace:Workspace=defaultWorkspace(); preferences:Preferences=defaultPreferences(); history:LogEntry[]=[];
  importedFrom?:string; warnings:string[]=[];
  private historyTimer?:ReturnType<typeof setTimeout>;
  constructor(readonly directory:string,private legacyDirectories:string[],private onError:(message:string)=>void){fs.mkdirSync(directory,{recursive:true});}
  private file(kind:string){return path.join(this.directory,`MultiSenderElectron.${kind}.json`);}
  private read(file:string):unknown {
    const size=fs.statSync(file).size;if(size>256*1024*1024)throw new Error(`Файл слишком большой: ${file}`);
    try{return JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));}catch(error){throw new Error(`Не удалось прочитать ${file}. Исходный файл сохранён. ${String(error)}`);}
  }
  private atomic(file:string,value:unknown){
    const temp=`${file}.${process.pid}.tmp`;
    try{fs.writeFileSync(temp,JSON.stringify(value,null,2),'utf8');fs.renameSync(temp,file);}catch(error){try{fs.unlinkSync(temp);}catch{}throw new Error(`Не удалось сохранить ${file}: ${String(error)}`);}
  }
  load(){
    for(const kind of ['workspace','history'] as const){
      const own=this.file(kind);
      const candidate=fs.existsSync(own)?own:this.legacyDirectories.flatMap(dir=>['MultiSender','HttpRequester'].map(name=>path.join(dir,`${name}.${kind}.json`))).find(file=>fs.existsSync(file));
      if(!candidate)continue;
      const data=this.read(candidate);
      if(kind==='workspace')this.workspace=normalizeWorkspace(data);else this.history=normalizeHistory(data);
      if(candidate!==own){this.importedFrom=path.dirname(candidate);if(kind==='workspace')this.atomic(own,this.workspace);else this.writeHistory();}
    }
    if(fs.existsSync(this.file('settings')))this.preferences=normalizePreferences(this.read(this.file('settings')));
    if(!fs.existsSync(this.file('workspace')))this.atomic(this.file('workspace'),this.workspace);
  }
  saveWorkspace(value:unknown){const next=normalizeWorkspace(value);this.atomic(this.file('workspace'),next);this.workspace=next;}
  savePreferences(value:unknown){const next=normalizePreferences(value);next.window=this.preferences.window;this.atomic(this.file('settings'),next);this.preferences=next;}
  saveWindow(value:Preferences['window']){this.preferences.window=value;this.atomic(this.file('settings'),this.preferences);}
  importFile(file:string){return normalizeWorkspace(this.read(file));}
  exportFile(file:string){this.atomic(file,this.workspace);}
  addLog(entry:LogEntry){
    const i=this.history.findIndex(item=>item.entryId===entry.entryId);if(i>=0)this.history[i]=entry;else this.history.push(entry);
    let count=0; for(let n=this.history.length-1;n>=0;n--){if(this.history[n].requestId===entry.requestId&&++count>1000)this.history.splice(n,1);}
    if(!this.historyTimer)this.historyTimer=setTimeout(()=>{this.historyTimer=undefined;try{this.writeHistory();}catch(error){this.onError(String(error));}},1000);
  }
  clearHistory(id?:string){const previous=this.history;this.history=id?this.history.filter(e=>e.requestId!==id&&e.collectionId!==id):[];try{this.writeHistory();}catch(error){this.history=previous;throw error;}}
  private writeHistory(){const requests:Record<string,LogEntry[]>=Object.create(null);for(const entry of this.history)(requests[entry.requestId]??=[]).push(entry);this.atomic(this.file('history'),{schemaVersion:1,requests});}
  flush(){if(this.historyTimer){clearTimeout(this.historyTimer);this.historyTimer=undefined;}this.writeHistory();this.atomic(this.file('settings'),this.preferences);}
}
