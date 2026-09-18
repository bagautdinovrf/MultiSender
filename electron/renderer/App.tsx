import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { DragEvent, KeyboardEvent, PointerEvent, ReactNode } from 'react';
import { Activity, ArrowDownToLine, ArrowUpFromLine, ArrowUpRight, Braces, Check, ChevronDown, ChevronRight, CircleAlert, Clock3, Code2, Copy, Database, Ellipsis, FileCode2, Folder, FolderOpen, FolderPlus, Hash, Layers3, Link2, LoaderCircle, Maximize2, Minus, Moon, Pencil, Play, Plus, Radio, RefreshCw, Search, Send, Settings2, Square, Sun, Terminal, Trash2, X, Zap } from 'lucide-react';
import { defaultPreferences, newCollection, newRequest, resolveRequest } from '../shared/model';
import type { Collection, Header, LogEntry, Preferences, Protocol, RequestDefinition, RuntimeState, SerialPortInfo, Workspace } from '../shared/types';

const resultLabels = ['В работе', 'Успешно', 'Ошибка HTTP', 'Ошибка сети', 'Отменено', 'Пропущено', 'Ошибка COM'];
const resultClass = (result: number) => result === 1 ? 'success' : [2, 3, 6].includes(result) ? 'error' : result === 0 ? 'pending' : 'neutral';
const isError = (entry: LogEntry) => [2, 3, 6].includes(entry.result);
const formatNumber = (n: number) => n.toLocaleString('ru-RU');
const russianPlural = new Intl.PluralRules('ru');
const plural = (count: number, one: string, few: string, many: string) => { const rule = russianPlural.select(count); return rule === 'one' ? one : rule === 'few' ? few : many; };
const formatTime = (value: string) => { const date = new Date(value); return Number.isNaN(date.getTime()) ? '—' : date.toLocaleTimeString('ru-RU', { hour12: false }); };
const formatBytes = (bytes: number) => bytes < 1024 ? `${bytes} Б` : bytes < 1048576 ? `${(bytes / 1024).toFixed(1)} КБ` : `${(bytes / 1048576).toFixed(1)} МБ`;
const clone = <T,>(value: T): T => structuredClone(value);
const encoder = new TextEncoder();
function payloadBytes(value: string, binary: boolean): Uint8Array {
  if (!binary) return encoder.encode(value);
  const bytes: number[] = [];
  let literalStart = 0;
  const append = (text: string) => { for (const byte of encoder.encode(text)) bytes.push(byte); };
  for (let index = 0; index < value.length;) {
    if (value[index] !== '\\' || !['\\', 'x', 'X'].includes(value[index + 1])) { index++; continue; }
    append(value.slice(literalStart, index));
    if (value[index + 1] === '\\') { bytes.push(92); index += 2; }
    else { const hex = value.slice(index + 2, index + 4); if (!/^[\da-f]{2}$/i.test(hex)) return encoder.encode(value); bytes.push(parseInt(hex, 16)); index += 4; }
    literalStart = index;
  }
  append(value.slice(literalStart));
  return new Uint8Array(bytes);
}
type Dialog = { type: 'newCollection' | 'newRequest' | 'rename' | 'delete' | 'clear' | 'import'; id?: string; value?: string };
type Toast = { id: number; message: string; kind: 'success' | 'error' | 'info' };
type Menu = { id: string; x: number; y: number };
type EditorTab = 'body' | 'headers' | 'settings';
type DetailTab = 'response' | 'request' | 'headers';

function IconButton({ children, title, onClick, className = '', disabled = false }: { children: ReactNode; title: string; onClick?: () => void; className?: string; disabled?: boolean }) {
  return <button type="button" className={`icon-button ${className}`} title={title} aria-label={title} onClick={onClick} disabled={disabled}>{children}</button>;
}
function Badge({ request, protocol, method }: { request?: RequestDefinition; protocol?: Protocol; method?: string }) {
  const label = (request?.protocol ?? protocol) === 'HTTP' ? (request?.method ?? method ?? 'GET') : (request?.protocol ?? protocol ?? 'HTTP');
  return <span className={`protocol-badge badge-${label.toLowerCase()}`}>{label}</span>;
}
function NumberField({ value, onChange, min = 1, max, suffix, disabled = false, label }: { value: number; onChange: (value: number) => void; min?: number; max?: number; suffix?: string; disabled?: boolean; label: string }) {
  return <div className={`number-input ${disabled ? 'disabled' : ''}`}><input aria-label={label} type="number" value={value} min={min} max={max} disabled={disabled} onChange={e => onChange(Number(e.target.value))} onBlur={e => { const v = Number(e.target.value); if (!Number.isFinite(v) || v < min) onChange(min); else if (max && v > max) onChange(max); }} />{suffix && <span>{suffix}</span>}</div>;
}
function Toggle({ checked, onChange, label }: { checked: boolean; onChange: (value: boolean) => void; label: string }) {
  return <button type="button" role="switch" aria-checked={checked} aria-label={label} className={`toggle ${checked ? 'checked' : ''}`} onClick={() => onChange(!checked)}><span /></button>;
}
function Inheritance({ inherited, onChange }: { inherited: boolean; onChange: () => void }) {
  return <button type="button" className={`inheritance ${inherited ? 'linked' : ''}`} aria-pressed={inherited} title={inherited ? 'Наследуется от коллекции. Нажмите, чтобы задать своё значение' : 'Своё значение. Нажмите, чтобы наследовать от коллекции'} onClick={onChange}><Link2 size={11} />{inherited ? 'Общее' : 'Своё'}</button>;
}

function BodyEditor({ value, onChange, binary, targetRef }: { value: string; onChange: (value: string) => void; binary: boolean; targetRef: React.RefObject<HTMLTextAreaElement | null> }) {
  const highlight = useRef<HTMLPreElement>(null);
  const numbers = useRef<HTMLDivElement>(null);
  const pieces = binary ? value.split(/(\\\\|\\[xX][0-9a-fA-F]{2})/g) : [value];
  const lineCount = value.split('\n').length;
  const handleKey = (e: KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key !== 'Tab' || e.shiftKey) return;
    e.preventDefault();
    const node = e.currentTarget, start = node.selectionStart, end = node.selectionEnd;
    onChange(`${value.slice(0, start)}  ${value.slice(end)}`);
    requestAnimationFrame(() => { node.selectionStart = node.selectionEnd = start + 2; });
  };
  return <div className="body-editor">
    <div className="line-numbers" ref={numbers}>{Array.from({ length: Math.max(4, lineCount) }, (_, index) => <div key={index}>{index + 1}</div>)}</div>
    <div className="code-input-wrap"><pre aria-hidden="true" className="code-highlight" ref={highlight}>{binary ? pieces.map((part, i) => /^\\[xX][\da-fA-F]{2}$/.test(part) ? <span className="hex-token" key={i}>{part}</span> : part) : <CodeText text={value} highlight={value.length < 100000} />}{'\n'}</pre>
      <textarea ref={targetRef} spellCheck={false} aria-label={binary ? 'Данные для отправки' : 'Тело запроса'} value={value} onChange={e => onChange(e.target.value)} onKeyDown={handleKey} onScroll={e => { if (highlight.current) { highlight.current.scrollTop = e.currentTarget.scrollTop; highlight.current.scrollLeft = e.currentTarget.scrollLeft; } if (numbers.current) numbers.current.scrollTop = e.currentTarget.scrollTop; }} placeholder={binary ? 'Введите текст или байты, например: HELLO\\x0D\\x0A' : '{\n  "message": "Hello, world!"\n}'} />
    </div>
  </div>;
}

function HeaderEditor({ headers, onChange }: { headers: Header[]; onChange: (headers: Header[]) => void }) {
  const update = (index: number, patch: Partial<Header>) => onChange(headers.map((item, i) => i === index ? { ...item, ...patch } : item));
  return <div className="headers-editor"><div className="header-columns"><span /><span>КЛЮЧ</span><span>ЗНАЧЕНИЕ</span><span /></div>
    {headers.map((header, index) => <div className="header-row" key={index}><input type="checkbox" aria-label={`Включить заголовок ${index + 1}`} checked={header.enabled !== false} onChange={e => update(index, { enabled: e.target.checked })} /><input aria-label={`Имя заголовка ${index + 1}`} placeholder="Content-Type" value={header.name} onChange={e => update(index, { name: e.target.value })} /><input aria-label={`Значение заголовка ${index + 1}`} placeholder="application/json" value={header.value} onChange={e => update(index, { value: e.target.value })} /><IconButton title="Удалить заголовок" onClick={() => onChange(headers.filter((_, i) => i !== index))}><X size={14} /></IconButton></div>)}
    <button className="text-button add-header" onClick={() => onChange([...headers, { name: '', value: '', enabled: true }])}><Plus size={14} />Добавить заголовок</button>
    {!headers.length && <p className="hint">Добавьте собственные HTTP-заголовки. Неактивные строки сохраняются, но не отправляются.</p>}
  </div>;
}

function Modal({ dialog, name, onClose, onSubmit, busy }: { dialog: Dialog; name: string; onClose: () => void; onSubmit: (value: string) => void; busy: boolean }) {
  const [value, setValue] = useState(dialog.value ?? '');
  const panel = useRef<HTMLDivElement>(null);
  const input = useRef<HTMLInputElement>(null);
  const nameMode = ['newCollection', 'newRequest', 'rename'].includes(dialog.type);
  const destructive = ['delete', 'clear'].includes(dialog.type);
  const title = ({ newCollection: 'Новая коллекция', newRequest: 'Новый запрос', rename: 'Переименовать', delete: 'Удалить элемент?', clear: 'Очистить журнал?', import: 'Импортировать пространство?' })[dialog.type];
  useEffect(() => { const node = input.current; if (node) { node.focus(); node.select(); } else panel.current?.focus(); }, []);
  const trapFocus = (e: KeyboardEvent<HTMLDivElement>) => {
    if (e.key === 'Escape' && !busy) onClose();
    if (e.key !== 'Tab') return;
    const focusable = panel.current?.querySelectorAll<HTMLElement>('button:not([disabled]), input:not([disabled])');
    if (!focusable?.length) return;
    const first = focusable[0], last = focusable[focusable.length - 1];
    if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
    if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
  };
  return <div className="modal-backdrop" onMouseDown={e => { if (e.target === e.currentTarget && !busy) onClose(); }}><div ref={panel} role="dialog" aria-modal="true" aria-labelledby="dialog-title" tabIndex={-1} className="modal" onKeyDown={trapFocus}>
    <div className={`modal-symbol ${destructive ? 'danger' : ''}`}>{destructive ? <Trash2 size={22} /> : dialog.type === 'import' ? <ArrowDownToLine size={22} /> : <FolderPlus size={22} />}</div>
    <h2 id="dialog-title">{title}</h2>
    {nameMode ? <><p>Дайте понятное название, чтобы легко найти его позже.</p><label className="field-label">Название<input ref={input} autoComplete="off" maxLength={160} value={value} onChange={e => setValue(e.target.value)} onKeyDown={e => { if (e.key === 'Enter' && value.trim() && !busy) onSubmit(value.trim()); }} placeholder={dialog.type === 'newRequest' ? 'Например, Проверка соединения' : 'Например, Локальный API'} /></label></> : dialog.type === 'delete' ? <p>«{name}» будет удалён{dialog.id && name ? ' из рабочего пространства' : ''}. Вложенные запросы также будут удалены. Это действие нельзя отменить.</p> : dialog.type === 'clear' ? <p>Записи журнала {name ? `для «${name}»` : 'всего рабочего пространства'} будут удалены. Настройки запросов сохранятся.</p> : <p>Выберите файл коллекций MultiSender. Текущее рабочее пространство будет заменено. При необходимости сначала экспортируйте его.</p>}
    <div className="modal-actions"><button className="button secondary" disabled={busy} onClick={onClose}>Отмена</button><button className={`button ${destructive ? 'danger-button' : 'primary'}`} disabled={busy || (nameMode && !value.trim())} onClick={() => onSubmit(value.trim())}>{busy && <LoaderCircle size={15} className="spin" />}{nameMode ? (dialog.type === 'rename' ? 'Сохранить' : 'Создать') : dialog.type === 'delete' ? 'Удалить' : dialog.type === 'clear' ? 'Очистить журнал' : 'Выбрать файл'}</button></div>
  </div></div>;
}

export default function App() {
  const [workspace, setWorkspace] = useState<Workspace>({ schemaVersion: 1, collections: [] });
  const [preferences, setPreferences] = useState<Preferences>(defaultPreferences);
  const [history, setHistory] = useState<LogEntry[]>([]);
  const [runtime, setRuntime] = useState<RuntimeState>({ running: [], active: {} });
  const [loading, setLoading] = useState(true);
  const [loadError, setLoadError] = useState('');
  const [version, setVersion] = useState('');
  const [dataDirectory, setDataDirectory] = useState('');
  const [importedFrom, setImportedFrom] = useState('');
  const [saveStatus, setSaveStatus] = useState<'saved' | 'saving' | 'error'>('saved');
  const [sidebarSearch, setSidebarSearch] = useState('');
  const [collapsed, setCollapsed] = useState<Set<string>>(new Set());
  const [editorTab, setEditorTab] = useState<EditorTab>('body');
  const [detailTab, setDetailTab] = useState<DetailTab>('response');
  const [detailMode, setDetailMode] = useState<'pretty' | 'raw' | 'hex'>('pretty');
  const [logSearch, setLogSearch] = useState('');
  const [logFilter, setLogFilter] = useState('all');
  const [selectedLogId, setSelectedLogId] = useState<string>('');
  const [logLimit, setLogLimit] = useState(200);
  const [dialog, setDialog] = useState<Dialog | null>(null);
  const [dialogBusy, setDialogBusy] = useState(false);
  const [menu, setMenu] = useState<Menu | null>(null);
  const [toasts, setToasts] = useState<Toast[]>([]);
  const [ports, setPorts] = useState<SerialPortInfo[]>([]);
  const [portsLoading, setPortsLoading] = useState(false);
  const [operationBusy, setOperationBusy] = useState(false);
  const [dropId, setDropId] = useState('');
  const [showShortcuts, setShowShortcuts] = useState(false);
  const workspaceRef = useRef(workspace);
  const prefsRef = useRef(preferences);
  const saveTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const preferenceTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const saveChain = useRef<Promise<void>>(Promise.resolve());
  const preferenceChain = useRef<Promise<void>>(Promise.resolve());
  const dirty = useRef(false);
  const preferencesDirty = useRef(false);
  const saveRevision = useRef(0);
  const preferenceRevision = useRef(0);
  const queuedLogs = useRef(new Map<string, LogEntry>());
  const toastCounter = useRef(0);
  const targetRef = useRef<HTMLInputElement>(null);
  const bodyRef = useRef<HTMLTextAreaElement>(null);
  const openTabsRef = useRef<HTMLDivElement>(null);
  const cleanupResize = useRef<(() => void) | null>(null);

  const notify = useCallback((message: string, kind: Toast['kind'] = 'error') => {
    const id = ++toastCounter.current;
    setToasts(items => [...items.slice(-3), { id, message, kind }]);
    setTimeout(() => setToasts(items => items.filter(item => item.id !== id)), kind === 'error' ? 9000 : 4500);
  }, []);
  const report = useCallback((error: unknown) => notify(error instanceof Error ? error.message.replace(/^Error invoking remote method '[^']+': (?:Error: )?/, '') : String(error)), [notify]);

  const queueSave = useCallback(() => {
    if (!dirty.current) return saveChain.current;
    const snapshot = clone(workspaceRef.current), revision = saveRevision.current;
    dirty.current = false;
    setSaveStatus('saving');
    const next = saveChain.current.catch(() => undefined).then(() => window.desktop.saveWorkspace(snapshot));
    saveChain.current = next;
    next.then(() => { if (revision === saveRevision.current) setSaveStatus('saved'); }, error => { if (revision === saveRevision.current) dirty.current = true; setSaveStatus('error'); report(error); });
    return next;
  }, [report]);
  const flushSave = useCallback(async () => { if (saveTimer.current) clearTimeout(saveTimer.current); do { await queueSave(); } while (dirty.current); }, [queueSave]);
  const updateWorkspace = useCallback((update: Workspace | ((value: Workspace) => Workspace)) => {
    const next = typeof update === 'function' ? update(workspaceRef.current) : update;
    workspaceRef.current = next;
    setWorkspace(next);
    dirty.current = true;
    saveRevision.current++;
    setSaveStatus('saving');
    if (saveTimer.current) clearTimeout(saveTimer.current);
    saveTimer.current = setTimeout(() => { void queueSave().catch(() => undefined); }, 420);
  }, [queueSave]);
  const queuePreferences = useCallback(() => {
    if (!preferencesDirty.current) return preferenceChain.current;
    preferencesDirty.current = false;
    const snapshot = clone(prefsRef.current), revision = preferenceRevision.current;
    preferenceChain.current = preferenceChain.current.catch(() => undefined).then(() => window.desktop.savePreferences(snapshot));
    preferenceChain.current.catch(error => { if (revision === preferenceRevision.current) preferencesDirty.current = true; report(error); });
    return preferenceChain.current;
  }, [report]);
  const updatePreferences = useCallback((patch: Partial<Preferences>) => {
    const next = { ...prefsRef.current, ...patch };
    prefsRef.current = next;
    setPreferences(next);
    preferencesDirty.current = true;
    preferenceRevision.current++;
    if (preferenceTimer.current) clearTimeout(preferenceTimer.current);
    preferenceTimer.current = setTimeout(() => { void queuePreferences().catch(() => undefined); }, 350);
  }, [queuePreferences]);

  useEffect(() => {
    if (!window.desktop) return;
    return window.desktop.onBeforeClose(async () => {
      if (preferenceTimer.current) clearTimeout(preferenceTimer.current);
      await flushSave();
      do { await queuePreferences(); } while (preferencesDirty.current);
    });
  }, [flushSave, queuePreferences]);

  useEffect(() => {
    let alive = true;
    if (!window.desktop) { setLoading(false); setLoadError('Запустите приложение через Electron. Подключение к основному процессу недоступно.'); return; }
    let loaded = false;
    const cleanup = [window.desktop.onLog(entry => queuedLogs.current.set(entry.entryId, entry)), window.desktop.onRuntime(setRuntime), window.desktop.onError(message => notify(message))];
    const batchTimer = setInterval(() => {
      if (!loaded || !queuedLogs.current.size) return;
      const pending = queuedLogs.current;
      queuedLogs.current = new Map();
      setHistory(items => {
        const updates = new Map(pending);
        const next = items.map(item => { const update = updates.get(item.entryId); if (update) updates.delete(item.entryId); return update ?? item; });
        next.push(...updates.values());
        const counts = new Map<string, number>();
        return next.reverse().filter(item => { const count = (counts.get(item.requestId) ?? 0) + 1; counts.set(item.requestId, count); return count <= 1000; }).reverse();
      });
    }, 80);
    window.desktop.load().then(snapshot => {
      if (!alive) return;
      workspaceRef.current = snapshot.workspace;
      prefsRef.current = snapshot.preferences;
      setWorkspace(snapshot.workspace); setPreferences(snapshot.preferences); setHistory(snapshot.history); setRuntime(snapshot.runtime);
      setVersion(snapshot.version); setDataDirectory(snapshot.dataDirectory); setImportedFrom(snapshot.importedFrom ?? '');
      setDetailMode(snapshot.preferences.prettyJson ? 'pretty' : 'raw');
      snapshot.warnings.forEach(message => notify(message, 'info'));
      loaded = true;
      setLoading(false);
    }).catch(error => { if (alive) { setLoadError(String(error)); setLoading(false); } });
    return () => { alive = false; clearInterval(batchTimer); cleanup.forEach(fn => fn()); cleanupResize.current?.(); };
  }, [notify]);
  useEffect(() => { document.documentElement.dataset.theme = preferences.theme; }, [preferences.theme]);
  useEffect(() => { setSelectedLogId(''); setLogLimit(200); }, [preferences.activeTab]);
  useEffect(() => { setLogLimit(200); }, [logSearch, logFilter]);
  useEffect(() => {
    const frame = requestAnimationFrame(() => openTabsRef.current?.querySelector<HTMLElement>('[data-active-tab="true"]')?.scrollIntoView({ block: 'nearest', inline: 'nearest' }));
    return () => cancelAnimationFrame(frame);
  }, [preferences.activeTab, preferences.tabs.length, loading]);
  useEffect(() => {
    const tabs = openTabsRef.current;
    if (!tabs) return;
    const wheel = (event: WheelEvent) => {
      if (tabs.scrollWidth <= tabs.clientWidth || event.ctrlKey) return;
      event.preventDefault();
      const delta = Math.abs(event.deltaX) > Math.abs(event.deltaY) ? event.deltaX : event.deltaY;
      tabs.scrollLeft += delta * (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? tabs.clientWidth : 1);
    };
    tabs.addEventListener('wheel', wheel, { passive: false });
    return () => tabs.removeEventListener('wheel', wheel);
  }, [loading]);

  const activeCollection = workspace.collections.find(c => c.id === preferences.activeTab || c.requests.some(r => r.id === preferences.activeTab));
  const activeRequest = activeCollection?.requests.find(r => r.id === preferences.activeTab);
  const resolved = activeCollection && activeRequest ? resolveRequest(activeCollection, activeRequest) : null;
  const activeId = activeRequest?.id ?? activeCollection?.id ?? '';
  const runningSet = new Set(runtime.running);
  const activeRunning = activeRequest ? runningSet.has(activeRequest.id) : !!activeCollection?.requests.some(r => runningSet.has(r.id));
  const activeCount = activeRequest ? (runtime.active[activeRequest.id] ?? 0) : activeCollection ? activeCollection.requests.reduce((total, r) => total + (runtime.active[r.id] ?? 0), 0) : Object.values(runtime.active).reduce((a, b) => a + b, 0);
  const activeBusy = activeRunning || activeCount > 0;
  const totalActive = Object.values(runtime.active).reduce((sum, count) => sum + count, 0);
  const requestBusy = (id: string) => runningSet.has(id) || (runtime.active[id] ?? 0) > 0;
  const requestCount = workspace.collections.reduce((count, c) => count + c.requests.length, 0);
  const lookup = (id: string) => { const collection = workspaceRef.current.collections.find(c => c.id === id || c.requests.some(r => r.id === id)); return { collection, request: collection?.requests.find(r => r.id === id) }; };
  const tabInfo = (id: string) => { const { collection, request } = lookup(id); return request ? { name: request.name, collection, request } : collection ? { name: collection.name, collection } : null; };
  const openTab = useCallback((id: string) => { const prefs = prefsRef.current; updatePreferences({ tabs: prefs.tabs.includes(id) ? prefs.tabs : [...prefs.tabs, id], activeTab: id }); }, [updatePreferences]);
  const closeTab = (id: string) => { const prefs = prefsRef.current; const index = prefs.tabs.indexOf(id); const tabs = prefs.tabs.filter(tab => tab !== id); updatePreferences({ tabs, activeTab: prefs.activeTab === id ? tabs[Math.max(0, index - 1)] ?? '' : prefs.activeTab }); };
  const patchRequest = (patch: Partial<RequestDefinition>) => { if (!activeCollection || !activeRequest) return; const cid = activeCollection.id, rid = activeRequest.id; updateWorkspace(ws => ({ ...ws, collections: ws.collections.map(c => c.id === cid ? { ...c, requests: c.requests.map(r => r.id === rid ? { ...r, ...patch } : r) } : c) })); };
  const patchCollection = (patch: Partial<Collection>) => { if (!activeCollection) return; updateWorkspace(ws => ({ ...ws, collections: ws.collections.map(c => c.id === activeCollection.id ? { ...c, ...patch } : c) })); };

  const act = useCallback(async (action: () => Promise<unknown>, save = true) => {
    if (operationBusy) return;
    setOperationBusy(true);
    try { if (save) await flushSave(); await action(); } catch (error) { report(error); } finally { setOperationBusy(false); }
  }, [flushSave, operationBusy, report]);
  const sendOnce = () => { if (activeRequest) void act(() => window.desktop.send(activeRequest.id)); };
  const toggleRun = (id = activeId) => { if (!id) return; const { collection, request } = lookup(id); const running = request ? runningSet.has(id) || (runtime.active[id] ?? 0) > 0 : collection?.requests.some(r => runningSet.has(r.id) || (runtime.active[r.id] ?? 0) > 0); void act(() => running ? window.desktop.stop(id) : window.desktop.start(id), !running); };
  useEffect(() => {
    const keydown = (e: globalThis.KeyboardEvent) => {
      if (dialog) return;
      if (e.ctrlKey && e.key === 'Enter') { e.preventDefault(); sendOnce(); }
      if (e.key === 'F5') { e.preventDefault(); toggleRun(); }
      if (e.ctrlKey && e.key.toLowerCase() === 'l') { e.preventDefault(); if (activeRequest?.protocol === 'HTTP') targetRef.current?.focus(); else { setEditorTab('body'); requestAnimationFrame(() => bodyRef.current?.focus()); } }
      if (e.key === 'Escape') { setMenu(null); setShowShortcuts(false); }
      if (e.ctrlKey && e.key.toLowerCase() === 's') { e.preventDefault(); void flushSave().catch(() => undefined); }
    };
    window.addEventListener('keydown', keydown);
    return () => window.removeEventListener('keydown', keydown);
  });

  const refreshPorts = async () => { setPortsLoading(true); try { setPorts(await window.desktop.listPorts()); } catch (error) { report(error); } finally { setPortsLoading(false); } };
  useEffect(() => { if (activeRequest?.protocol === 'COM') void refreshPorts(); }, [activeRequest?.id, activeRequest?.protocol]);
  useEffect(() => { if (activeRequest?.protocol !== 'HTTP' && editorTab === 'headers') setEditorTab('body'); }, [activeRequest?.protocol, editorTab]);
  const showMenu = (id: string, button: HTMLElement) => { const rect = button.getBoundingClientRect(); setMenu({ id, x: Math.min(rect.left, window.innerWidth - 220), y: Math.min(rect.bottom + 4, window.innerHeight - 240) }); };
  const duplicate = (id: string) => { const { collection, request } = lookup(id); if (!collection) return;
    if (request) { const copy = { ...clone(request), id: crypto.randomUUID(), name: `${request.name} — копия` }; updateWorkspace(ws => ({ ...ws, collections: ws.collections.map(c => c.id === collection.id ? { ...c, requests: [...c.requests, copy] } : c) })); openTab(copy.id); }
    else { const copy = { ...clone(collection), id: crypto.randomUUID(), name: `${collection.name} — копия`, requests: collection.requests.map(r => ({ ...clone(r), id: crypto.randomUUID() })) }; updateWorkspace(ws => ({ ...ws, collections: [...ws.collections, copy] })); openTab(copy.id); }
    setMenu(null); notify('Копия создана', 'success');
  };
  const submitDialog = async (value: string) => {
    if (!dialog) return;
    setDialogBusy(true);
    try {
      if (dialog.type === 'newCollection') { const collection = newCollection(value); updateWorkspace(ws => ({ ...ws, collections: [...ws.collections, collection] })); openTab(collection.id); }
      if (dialog.type === 'newRequest') { const collection = workspaceRef.current.collections.find(c => c.id === dialog.id); if (!collection) return; const request = newRequest(value); updateWorkspace(ws => ({ ...ws, collections: ws.collections.map(c => c.id === collection.id ? { ...c, requests: [...c.requests, request] } : c) })); setCollapsed(items => { const next = new Set(items); next.delete(collection.id); return next; }); openTab(request.id); }
      if (dialog.type === 'rename') updateWorkspace(ws => ({ ...ws, collections: ws.collections.map(c => c.id === dialog.id ? { ...c, name: value } : { ...c, requests: c.requests.map(r => r.id === dialog.id ? { ...r, name: value } : r) }) }));
      if (dialog.type === 'delete' && dialog.id) {
        await window.desktop.stop(dialog.id);
        const { collection, request } = lookup(dialog.id); const removed = request ? [request.id] : collection ? [collection.id, ...collection.requests.map(r => r.id)] : [];
        updateWorkspace(ws => ({ ...ws, collections: ws.collections.filter(c => c.id !== dialog.id).map(c => ({ ...c, requests: c.requests.filter(r => r.id !== dialog.id) })) }));
        const tabs = prefsRef.current.tabs.filter(id => !removed.includes(id)); updatePreferences({ tabs, activeTab: removed.includes(prefsRef.current.activeTab) ? tabs[tabs.length - 1] ?? '' : prefsRef.current.activeTab });
      }
      if (dialog.type === 'clear') { await window.desktop.clearHistory(dialog.id || undefined); for (const [key, entry] of queuedLogs.current) { if (!dialog.id || entry.requestId === dialog.id || entry.collectionId === dialog.id) queuedLogs.current.delete(key); } setHistory(items => items.filter(item => dialog.id && item.requestId !== dialog.id && item.collectionId !== dialog.id)); setSelectedLogId(''); notify('Журнал очищен', 'success'); }
      if (dialog.type === 'import') { await flushSave(); const imported = await window.desktop.importWorkspace(); if (imported) { await window.desktop.stop(); updateWorkspace(imported); await flushSave(); updatePreferences({ tabs: [], activeTab: '' }); notify('Рабочее пространство импортировано', 'success'); } }
      setDialog(null);
    } catch (error) { report(error); } finally { setDialogBusy(false); }
  };
  const exportData = () => void act(async () => { if (await window.desktop.exportWorkspace()) notify('Рабочее пространство экспортировано', 'success'); });
  const closeWindow = () => void act(async () => { if (preferenceTimer.current) clearTimeout(preferenceTimer.current); await queuePreferences(); window.desktop.windowAction('close'); });
  const startDrag = (event: DragEvent, id: string, kind: 'collection' | 'request') => { event.dataTransfer.effectAllowed = 'move'; event.dataTransfer.setData('application/x-multisender', JSON.stringify({ id, kind })); };
  const drop = (event: DragEvent, targetId: string, targetKind: 'collection' | 'request') => {
    event.preventDefault(); event.stopPropagation(); setDropId('');
    if (!event.dataTransfer.types.includes('application/x-multisender') || event.dataTransfer.files.length) return;
    const targetRect = event.currentTarget.getBoundingClientRect();
    const insertAfter = event.clientY > targetRect.top + targetRect.height / 2;
    try { const payload = JSON.parse(event.dataTransfer.getData('application/x-multisender')) as { id: string; kind: string }; if (payload.id === targetId) return;
      if (payload.kind === 'collection' && targetKind === 'collection') updateWorkspace(ws => { const items = [...ws.collections], source = items.findIndex(c => c.id === payload.id), target = items.findIndex(c => c.id === targetId); if (source < 0 || target < 0) return ws; const [item] = items.splice(source, 1); items.splice(items.findIndex(c => c.id === targetId) + (insertAfter ? 1 : 0), 0, item); return { ...ws, collections: items }; });
      else if (payload.kind === 'request') updateWorkspace(ws => { const source = ws.collections.find(c => c.requests.some(r => r.id === payload.id)); const item = source?.requests.find(r => r.id === payload.id); const target = ws.collections.find(c => targetKind === 'collection' ? c.id === targetId : c.requests.some(r => r.id === targetId)); if (!item || !target) return ws; return { ...ws, collections: ws.collections.map(c => { const requests = c.requests.filter(r => r.id !== payload.id); if (c.id === target.id) { const index = targetKind === 'request' ? requests.findIndex(r => r.id === targetId) + (insertAfter ? 1 : 0) : requests.length; requests.splice(Math.max(0, index), 0, item); } return { ...c, requests }; }) }; });
    } catch { /* Ignore unrelated drags. */ }
  };
  const dragOver = (event: DragEvent, id: string) => { if (event.dataTransfer.types.includes('application/x-multisender')) { event.preventDefault(); event.stopPropagation(); event.dataTransfer.dropEffect = 'move'; setDropId(id); } };
  const beginResize = (event: PointerEvent, dimension: 'sidebarWidth' | 'editorHeight' | 'detailHeight') => {
    event.preventDefault(); cleanupResize.current?.();
    const initial = prefsRef.current[dimension], origin = dimension === 'sidebarWidth' ? event.clientX : event.clientY;
    document.body.classList.add(dimension === 'sidebarWidth' ? 'resize-horizontal' : 'resize-vertical');
    const move = (e: globalThis.PointerEvent) => { const delta = (dimension === 'sidebarWidth' ? e.clientX : e.clientY) - origin; const min = dimension === 'sidebarWidth' ? 210 : dimension === 'editorHeight' ? 220 : 100; const max = dimension === 'sidebarWidth' ? 410 : Math.max(min, window.innerHeight - 300); updatePreferences({ [dimension]: Math.min(max, Math.max(min, initial + (dimension === 'detailHeight' ? -delta : delta))) }); };
    const finish = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', finish); document.body.classList.remove('resize-horizontal', 'resize-vertical'); cleanupResize.current = null; };
    cleanupResize.current = finish; window.addEventListener('pointermove', move); window.addEventListener('pointerup', finish, { once: true });
  };

  const scopedHistory = useMemo(() => history.filter(item => !activeId || item.requestId === activeId || item.collectionId === activeId), [history, activeId]);
  const filteredHistory = useMemo(() => { const term = logSearch.toLowerCase(); return scopedHistory.filter(item => (logFilter === 'all' || (logFilter === 'success' ? item.result === 1 : logFilter === 'error' ? isError(item) : logFilter === 'pending' ? item.result === 0 : [4, 5].includes(item.result))) && (!term || `${item.requestName} ${item.url} ${item.httpStatus} ${item.responseBody} ${item.requestBody} ${item.errorText}`.toLowerCase().includes(term))).slice().reverse(); }, [scopedHistory, logFilter, logSearch]);
  const selectedLog = scopedHistory.find(item => item.entryId === selectedLogId) ?? null;
  const completed = scopedHistory.filter(entry => entry.result !== 0 && entry.result !== 5);
  const successes = completed.filter(entry => entry.result === 1).length;
  const meanTime = completed.length ? Math.round(completed.reduce((sum, item) => sum + item.elapsedMs, 0) / completed.length) : 0;
  const changeDetailMode = (mode: 'pretty' | 'raw' | 'hex') => { setDetailMode(mode); if (mode !== 'hex') updatePreferences({ prettyJson: mode === 'pretty' }); };
  const detailText = useMemo(() => {
    if (!selectedLog) return '';
    const value = detailTab === 'request' ? selectedLog.requestBody : selectedLog.responseBody;
    if (detailMode === 'hex') { const hex = detailTab === 'response' && selectedLog.responseHex ? selectedLog.responseHex.replace(/\s/g, '') : Array.from(payloadBytes(value, detailTab === 'request' && selectedLog.protocol !== 'HTTP'), byte => byte.toString(16).padStart(2, '0')).join(''); const bytes = hex.match(/.{1,2}/g) ?? []; return Array.from({ length: Math.ceil(bytes.length / 16) }, (_, index) => `${(index * 16).toString(16).padStart(8, '0')}   ${bytes.slice(index * 16, index * 16 + 16).join(' ').toUpperCase().padEnd(47, ' ')}   ${bytes.slice(index * 16, index * 16 + 16).map(byte => { const value = parseInt(byte, 16); return value >= 32 && value <= 126 ? String.fromCharCode(value) : '·'; }).join('')}`).join('\n'); }
    if (detailMode === 'pretty') { try { return JSON.stringify(JSON.parse(value), null, 2); } catch { return value; } }
    return value;
  }, [selectedLog, detailTab, detailMode]);
  const copyText = async (text: string) => { try { await navigator.clipboard.writeText(text); notify('Скопировано в буфер обмена', 'success'); } catch (error) { report(error); } };
  const renameDialog = (id: string) => { setDialog({ type: 'rename', id, value: tabInfo(id)?.name }); setMenu(null); };
  const collectionMatches = (collection: Collection) => !sidebarSearch || collection.name.toLowerCase().includes(sidebarSearch.toLowerCase()) || collection.requests.some(r => `${r.name} ${r.protocol} ${r.method}`.toLowerCase().includes(sidebarSearch.toLowerCase()));
  const resizeHandle = (dimension: 'sidebarWidth' | 'editorHeight' | 'detailHeight') => <div className={`resize-handle ${dimension === 'sidebarWidth' ? 'vertical' : 'horizontal'}`} role="separator" aria-label={dimension === 'sidebarWidth' ? 'Ширина панели коллекций' : dimension === 'editorHeight' ? 'Высота редактора' : 'Высота подробностей'} aria-orientation={dimension === 'sidebarWidth' ? 'vertical' : 'horizontal'} tabIndex={0} onPointerDown={e => beginResize(e, dimension)} onKeyDown={e => { if (['ArrowUp', 'ArrowLeft', 'ArrowDown', 'ArrowRight'].includes(e.key)) { e.preventDefault(); const delta = ['ArrowUp', 'ArrowLeft'].includes(e.key) ? -20 : 20; const min = dimension === 'sidebarWidth' ? 210 : dimension === 'editorHeight' ? 220 : 100; const max = dimension === 'sidebarWidth' ? 410 : Math.max(min, window.innerHeight - 300); updatePreferences({ [dimension]: Math.max(min, Math.min(max, prefsRef.current[dimension] + delta)) }); } }} />;

  return <div className="app-shell">
    <header className="titlebar"><button className="brand" onClick={() => updatePreferences({ activeTab: '' })} title="Открыть обзор пространства"><span className="brand-mark"><Zap size={17} fill="currentColor" /></span><strong>MultiSender</strong><span className="edition">ELECTRON</span></button><div className="titlebar-center"><span className="workspace-dot" />Рабочее пространство<span className="title-separator">/</span><span>Локально</span></div><div className="titlebar-actions"><IconButton title={preferences.theme === 'dark' ? 'Светлая тема' : 'Тёмная тема'} onClick={() => updatePreferences({ theme: preferences.theme === 'dark' ? 'light' : 'dark' })}>{preferences.theme === 'dark' ? <Sun size={15} /> : <Moon size={15} />}</IconButton><span className="window-divider" /><IconButton title="Свернуть" onClick={() => window.desktop?.windowAction('minimize')} className="window-action"><Minus size={14} /></IconButton><IconButton title="Развернуть / восстановить" onClick={() => window.desktop?.windowAction('maximize')} className="window-action"><Maximize2 size={12} /></IconButton><IconButton title="Закрыть окно" onClick={closeWindow} className="window-action window-close"><X size={17} /></IconButton></div></header>
    {loading ? <div className="loading-screen"><span className="brand-mark large"><Zap size={28} fill="currentColor" /></span><h2>Ваше рабочее пространство</h2><p><LoaderCircle className="spin" size={16} />Загружаем коллекции и историю…</p></div> : loadError ? <div className="loading-screen"><CircleAlert size={36} /><h2>Не удалось загрузить пространство</h2><p>{loadError}</p><button className="button secondary" onClick={() => window.location.reload()}>Повторить</button></div> : <>
      <div className="workbench">
        <aside className="sidebar" style={{ width: preferences.sidebarWidth }}>
          <div className="sidebar-workspace"><span className="workspace-avatar"><Layers3 size={19} /></span><div><strong>Моё пространство</strong><span>Коллекции и подключения</span></div><IconButton title="Новая коллекция" onClick={() => setDialog({ type: 'newCollection' })}><Plus size={18} /></IconButton></div>
          <div className="sidebar-search"><Search size={14} /><input placeholder="Найти коллекцию или запрос" aria-label="Поиск в коллекциях" value={sidebarSearch} onChange={e => setSidebarSearch(e.target.value)} />{sidebarSearch && <IconButton title="Сбросить поиск" onClick={() => setSidebarSearch('')}><X size={12} /></IconButton>}</div>
          <div className="section-label"><span>КОЛЛЕКЦИИ <em>{workspace.collections.length}</em></span><IconButton title="Добавить коллекцию" onClick={() => setDialog({ type: 'newCollection' })}><FolderPlus size={15} /></IconButton></div>
          <nav className="collection-tree" aria-label="Коллекции и запросы" onDragLeave={e => { if (!e.currentTarget.contains(e.relatedTarget as Node)) setDropId(''); }}>
            {workspace.collections.filter(collectionMatches).map(collection => <div key={collection.id} className="tree-group">
              <div className={`collection-row ${preferences.activeTab === collection.id ? 'selected' : ''} ${dropId === collection.id ? 'drop-target' : ''}`} draggable onDragStart={e => startDrag(e, collection.id, 'collection')} onDragOver={e => dragOver(e, collection.id)} onDrop={e => drop(e, collection.id, 'collection')}>
                <button className="tree-chevron" aria-label={collapsed.has(collection.id) ? 'Развернуть коллекцию' : 'Свернуть коллекцию'} onClick={() => setCollapsed(items => { const next = new Set(items); next.has(collection.id) ? next.delete(collection.id) : next.add(collection.id); return next; })}>{collapsed.has(collection.id) ? <ChevronRight size={13} /> : <ChevronDown size={13} />}</button>
                <button className="tree-item-main" onClick={() => openTab(collection.id)} onDoubleClick={() => renameDialog(collection.id)}><Folder size={15} /><span>{collection.name}</span></button><span className="tree-count">{collection.requests.length}</span><button className="icon-button tree-more" title="Действия с коллекцией" aria-label={`Действия: ${collection.name}`} onClick={e => showMenu(collection.id, e.currentTarget)}><Ellipsis size={16} /></button>
              </div>
              {(!collapsed.has(collection.id) || sidebarSearch) && <div className="request-tree">{collection.requests.filter(r => !sidebarSearch || collection.name.toLowerCase().includes(sidebarSearch.toLowerCase()) || `${r.name} ${r.protocol} ${r.method}`.toLowerCase().includes(sidebarSearch.toLowerCase())).map(request => <div key={request.id} className={`request-row ${preferences.activeTab === request.id ? 'selected' : ''} ${dropId === request.id ? 'drop-target' : ''}`} draggable onDragStart={e => { e.stopPropagation(); startDrag(e, request.id, 'request'); }} onDragOver={e => dragOver(e, request.id)} onDrop={e => drop(e, request.id, 'request')}><button className="tree-item-main" onClick={() => openTab(request.id)} onDoubleClick={() => renameDialog(request.id)}><Badge request={request} /><span>{request.name}</span>{runningSet.has(request.id) && <span className="running-dot" title="Периодическая отправка" />}</button><button className="icon-button tree-more" title="Действия с запросом" aria-label={`Действия: ${request.name}`} onClick={e => showMenu(request.id, e.currentTarget)}><Ellipsis size={15} /></button></div>)}<button className="tree-add" onClick={() => setDialog({ type: 'newRequest', id: collection.id })}><Plus size={13} />Новый запрос</button></div>}
            </div>)}
            {!workspace.collections.filter(collectionMatches).length && <div className="tree-empty"><Search size={22} /><span>{sidebarSearch ? 'Ничего не найдено' : 'Здесь будут ваши коллекции'}</span>{!sidebarSearch && <button className="text-button" onClick={() => setDialog({ type: 'newCollection' })}>Создать коллекцию</button>}</div>}
          </nav>
          <div className="sidebar-bottom"><div className="transfer-actions"><button title="Импортировать коллекции из JSON" onClick={() => setDialog({ type: 'import' })}><ArrowDownToLine size={14} />Импорт</button><button title="Экспортировать пространство в JSON" onClick={exportData}><ArrowUpFromLine size={14} />Экспорт</button></div><button className="data-directory" title={dataDirectory} onClick={() => void window.desktop.openDataDirectory().catch(report)}><Database size={15} /><div><strong>Данные на этом компьютере</strong><span>Открыть папку пространства</span></div><ArrowUpRight size={14} /></button></div>
        </aside>
        {resizeHandle('sidebarWidth')}
        <main className="main-panel">
          <div className="tabbar"><button className={`home-tab ${!activeId ? 'active' : ''}`} aria-label="Обзор пространства" title="Обзор пространства" onClick={() => updatePreferences({ activeTab: '' })}><Layers3 size={16} /></button><div className="open-tabs" ref={openTabsRef}>{preferences.tabs.map(id => { const info = tabInfo(id); return info ? <div key={id} data-active-tab={preferences.activeTab === id ? "true" : undefined} className={`workspace-tab ${preferences.activeTab === id ? 'active' : ''}`}><button className="tab-open" onClick={() => openTab(id)}>{info.request ? <Badge request={info.request} /> : <Folder size={14} />}<span>{info.name}</span>{info.request && runningSet.has(id) && <span className="running-dot" />}</button><button className="tab-close" aria-label={`Закрыть вкладку ${info.name}`} title="Закрыть вкладку" onClick={() => closeTab(id)}><X size={12} /></button></div> : null; })}</div><IconButton title="Новая коллекция" className="tab-plus" onClick={() => setDialog({ type: 'newCollection' })}><Plus size={16} /></IconButton></div>
          {importedFrom && <div className="import-banner"><Check size={14} /><span>Коллекции и история скопированы из Qt-версии. Работа продолжается в отдельном пространстве.</span><IconButton title="Скрыть сообщение" onClick={() => setImportedFrom('')}><X size={13} /></IconButton></div>}
          <div className="editor-region" style={{ height: Math.min(preferences.editorHeight, Math.max(220, window.innerHeight - 300)) }}>
            {activeRequest && activeCollection && resolved ? <>
              <div className="editor-heading"><div className="breadcrumb"><button onClick={() => openTab(activeCollection.id)}><Folder size={13} />{activeCollection.name}</button><ChevronRight size={12} /><span>{activeRequest.name}</span></div><button className="text-button rename-action" title="Переименовать запрос" onClick={() => renameDialog(activeRequest.id)}><Pencil size={12} />Переименовать</button></div>
              <div className="request-command"><div className="address-bar"><select className={`protocol-select protocol-${activeRequest.protocol.toLowerCase()}`} aria-label="Протокол" value={activeRequest.protocol} onChange={e => { patchRequest({ protocol: e.target.value as Protocol }); setEditorTab('body'); }}><option>HTTP</option><option>TCP</option><option>UDP</option><option>COM</option></select>{activeRequest.protocol === 'HTTP' && <select className={`method-select method-${activeRequest.method.toLowerCase()}`} aria-label="HTTP-метод" value={activeRequest.method} onChange={e => patchRequest({ method: e.target.value as 'GET' | 'POST' })}><option>GET</option><option>POST</option></select>}{activeRequest.protocol === 'HTTP' ? <input ref={targetRef} className="target-input" aria-label="Путь HTTP-запроса" placeholder="/api/v1/resource" value={activeRequest.target} onChange={e => patchRequest({ target: e.target.value })} /> : activeRequest.protocol === 'COM' ? <input aria-label="Имя COM-порта" list="serial-ports" className="target-input" value={activeRequest.serial.portName} onChange={e => patchRequest({ serial: { ...activeRequest.serial, portName: e.target.value } })} /> : <div className="connection-target"><span>{resolved.host}</span><span className="subtle">:</span><span>{resolved.port}</span><span className="connection-kind">{activeRequest.protocol === 'TCP' ? 'Поток байтов' : 'Датаграмма'}</span></div>}</div><button className="button primary send-button" disabled={operationBusy} onClick={sendOnce} title="Отправить один раз · Ctrl+Enter">{operationBusy ? <LoaderCircle size={15} className="spin" /> : <Send size={15} />}<span>Отправить</span><kbd>↵</kbd></button><button className={`button run-button ${activeBusy ? 'is-running' : 'secondary'}`} disabled={operationBusy} onClick={() => toggleRun()} title={activeBusy ? 'Остановить отправку · F5' : 'Запустить периодическую отправку · F5'}>{activeBusy ? <Square size={13} fill="currentColor" /> : <Play size={14} />}<span>{activeBusy ? 'Стоп' : 'Цикл'}</span></button></div>
              <div className="connection-summary"><span><span className={`connection-dot ${activeRunning ? 'live' : ''}`} />{activeRunning ? 'Периодическая отправка' : activeCount ? 'Выполняется запрос' : 'Готов к отправке'}</span><span className="summary-divider" />{activeRequest.protocol === 'COM' ? <span>{activeRequest.serial.baudRate} бод · {activeRequest.serial.dataBits} бит</span> : <span className="mono">{resolved.host}:{resolved.port}</span>}<span><Clock3 size={12} />{formatNumber(resolved.intervalMs)} мс</span>{activeRequest.protocol !== 'COM' && <span><Layers3 size={12} />до {resolved.maxInFlight} параллельно</span>}{activeCount > 0 && <span className="accent-text">{activeCount} в работе</span>}</div>
              <div className="editor-tabs"><button className={editorTab === 'body' ? 'active' : ''} onClick={() => setEditorTab('body')}><Code2 size={14} />{activeRequest.protocol === 'HTTP' ? 'Тело запроса' : 'Данные'}</button>{activeRequest.protocol === 'HTTP' && <button className={editorTab === 'headers' ? 'active' : ''} onClick={() => setEditorTab('headers')}><ListIcon />Заголовки<span className="small-count">{activeRequest.headers.filter(h => h.enabled !== false && h.name).length}</span></button>}<button className={editorTab === 'settings' ? 'active' : ''} onClick={() => setEditorTab('settings')}><Settings2 size={14} />Настройки</button><span className="editor-tab-aside">{editorTab === 'body' ? activeRequest.protocol === 'HTTP' ? 'UTF-8' : 'UTF-8 + HEX' : editorTab === 'headers' ? 'HTTP HEADERS' : 'КОНФИГУРАЦИЯ'}</span></div>
              <div className={`editor-content ${editorTab === 'body' ? 'body-content' : ''}`}>
                {editorTab === 'body' ? <><BodyEditor key={activeRequest.id} value={activeRequest.body} onChange={body => patchRequest({ body })} binary={activeRequest.protocol !== 'HTTP'} targetRef={bodyRef} /><div className="editor-footnote">{activeRequest.protocol === 'HTTP' ? <span>{activeRequest.method === 'GET' ? 'GET-запрос отправляется без тела. Для передачи данных выберите POST.' : 'Данные передаются как UTF-8. Content-Type задаётся в заголовках.'}</span> : <span>Байты: <code>\x00</code>–<code>\xFF</code><span className="dot-separator">·</span>Разделители не добавляются</span>}<span>{formatNumber(new TextEncoder().encode(activeRequest.body).length)} {activeRequest.protocol === 'HTTP' ? 'байт' : 'байт текста'}</span></div></> : editorTab === 'headers' ? <HeaderEditor headers={activeRequest.headers} onChange={headers => patchRequest({ headers })} /> : <div className="request-settings">
                  <div className="settings-grid">
                    {activeRequest.protocol !== 'COM' && <><label className="field-label"><span>Адрес<Inheritance inherited={!activeRequest.overrideHost} onChange={() => patchRequest({ overrideHost: !activeRequest.overrideHost, host: resolved.host })} /></span><input aria-label="Адрес сервера" disabled={!activeRequest.overrideHost} value={resolved.host} onChange={e => patchRequest({ host: e.target.value })} /></label><label className="field-label"><span>Порт<Inheritance inherited={!activeRequest.overridePort} onChange={() => patchRequest({ overridePort: !activeRequest.overridePort, port: resolved.port })} /></span><NumberField label="Порт сервера" min={1} max={65535} disabled={!activeRequest.overridePort} value={resolved.port} onChange={port => patchRequest({ port })} /></label></>}
                    <label className="field-label"><span>Интервал<Inheritance inherited={!activeRequest.overrideIntervalMs} onChange={() => patchRequest({ overrideIntervalMs: !activeRequest.overrideIntervalMs, intervalMs: resolved.intervalMs })} /></span><NumberField label="Интервал отправки" value={resolved.intervalMs} suffix="мс" disabled={!activeRequest.overrideIntervalMs} onChange={intervalMs => patchRequest({ intervalMs })} /></label>
                    {activeRequest.protocol !== 'COM' && <label className="field-label"><span>Параллельно<Inheritance inherited={!activeRequest.overrideMaxInFlight} onChange={() => patchRequest({ overrideMaxInFlight: !activeRequest.overrideMaxInFlight, maxInFlight: resolved.maxInFlight })} /></span><NumberField label="Лимит параллельных запросов" value={resolved.maxInFlight} disabled={!activeRequest.overrideMaxInFlight} onChange={maxInFlight => patchRequest({ maxInFlight })} /></label>}
                    {activeRequest.protocol !== 'HTTP' && <label className="field-label"><span>Таймаут ответа</span><NumberField label="Таймаут ответа" value={activeRequest.timeoutMs} suffix="мс" onChange={timeoutMs => patchRequest({ timeoutMs })} /></label>}
                    {activeRequest.protocol !== 'HTTP' && <div className="setting-toggle"><div><strong>Ожидать ответ</strong><span>В пределах таймаута</span></div><Toggle checked={activeRequest.waitForResponse} onChange={waitForResponse => patchRequest({ waitForResponse })} label="Ожидать ответ" /></div>}
                  </div>
                  {activeRequest.protocol === 'COM' && <><div className="settings-section-heading"><span>ПАРАМЕТРЫ COM-ПОРТА</span><button className="text-button" disabled={portsLoading} onClick={() => void refreshPorts()}><RefreshCw size={12} className={portsLoading ? 'spin' : ''} />Обновить порты</button></div><div className="settings-grid serial-grid"><label className="field-label">Порт<input list="serial-ports" value={activeRequest.serial.portName} onChange={e => patchRequest({ serial: { ...activeRequest.serial, portName: e.target.value } })} /></label><label className="field-label">Скорость, бод<NumberField label="Скорость COM-порта" value={activeRequest.serial.baudRate} onChange={baudRate => patchRequest({ serial: { ...activeRequest.serial, baudRate } })} /></label><label className="field-label">Биты данных<select value={activeRequest.serial.dataBits} onChange={e => { const dataBits = Number(e.target.value) as 5 | 6 | 7 | 8; patchRequest({ serial: { ...activeRequest.serial, dataBits, stopBits: dataBits === 5 ? activeRequest.serial.stopBits === 2 ? 1 : activeRequest.serial.stopBits : activeRequest.serial.stopBits === 3 ? 1 : activeRequest.serial.stopBits } }); }}><option>8</option><option>7</option><option>6</option><option>5</option></select></label><label className="field-label">Чётность<select value={activeRequest.serial.parity} onChange={e => patchRequest({ serial: { ...activeRequest.serial, parity: Number(e.target.value) } })}><option value={0}>Нет</option><option value={2}>Чётная</option><option value={3}>Нечётная</option><option value={4}>Space</option><option value={5}>Mark</option></select></label><label className="field-label">Стоп-биты<select value={activeRequest.serial.stopBits} onChange={e => patchRequest({ serial: { ...activeRequest.serial, stopBits: Number(e.target.value) } })}><option value={1}>1</option>{activeRequest.serial.dataBits === 5 ? <option value={3}>1.5</option> : <option value={2}>2</option>}</select></label><label className="field-label">Управление потоком<select value={activeRequest.serial.flowControl} onChange={e => patchRequest({ serial: { ...activeRequest.serial, flowControl: Number(e.target.value) } })}><option value={0}>Нет</option><option value={1}>RTS/CTS</option><option value={2}>XON/XOFF</option></select></label></div><p className="hint">Один запрос — одна операция на порту. Порт освобождается после завершения или остановки.</p></>}
                  <p className="hint"><Link2 size={12} />Параметры с меткой «Общее» наследуются от коллекции.{activeRequest.protocol === 'HTTP' && ' Таймаут бездействия HTTP — 30 секунд.'}{activeRunning && ' Изменения применятся после остановки и повторного запуска.'}</p>
                </div>}
              </div>
            </> : activeCollection ? <div className="collection-editor"><div className="collection-heading"><span className="collection-symbol"><FolderOpen size={23} /></span><div><div className="eyebrow">КОЛЛЕКЦИЯ</div><h1>{activeCollection.name}<IconButton title="Переименовать коллекцию" onClick={() => renameDialog(activeCollection.id)}><Pencil size={13} /></IconButton></h1></div><div className="collection-heading-actions"><button className="button secondary" onClick={() => setDialog({ type: 'newRequest', id: activeCollection.id })}><Plus size={14} />Новый запрос</button><button className={`button ${activeBusy ? 'is-running' : 'primary'}`} disabled={operationBusy || !activeCollection.requests.length} onClick={() => toggleRun()}>{activeBusy ? <Square size={13} /> : <Play size={14} />}{activeBusy ? 'Остановить все' : 'Запустить все'}</button></div></div><p className="collection-description">Общие настройки для запросов коллекции. Любой параметр можно переопределить в запросе.</p><div className="collection-settings"><label className="field-label">Адрес сервера<input value={activeCollection.host} onChange={e => patchCollection({ host: e.target.value })} /></label><label className="field-label">Порт<NumberField label="Порт коллекции" value={activeCollection.port} max={65535} onChange={port => patchCollection({ port })} /></label><label className="field-label">Интервал отправки<NumberField label="Интервал коллекции" value={activeCollection.intervalMs} suffix="мс" onChange={intervalMs => patchCollection({ intervalMs })} /></label><label className="field-label">Лимит на запрос<NumberField label="Лимит параллельности коллекции" value={activeCollection.maxInFlight} onChange={maxInFlight => patchCollection({ maxInFlight })} /></label></div><div className="collection-list-heading"><span>ЗАПРОСЫ <em>{activeCollection.requests.length}</em></span><span>{activeCollection.requests.filter(r => runningSet.has(r.id)).length} запущено</span></div><div className="collection-request-list">{activeCollection.requests.map(request => <div className="collection-request-item" key={request.id}><button onClick={() => openTab(request.id)}><Badge request={request} /><strong>{request.name}</strong><span className="request-endpoint">{request.protocol === 'HTTP' ? request.target : request.protocol === 'COM' ? request.serial.portName : `${resolveRequest(activeCollection, request).host}:${resolveRequest(activeCollection, request).port}`}</span><ArrowUpRight size={13} /></button><span className={`request-state ${runningSet.has(request.id) ? 'accent-text' : ''}`}>{runningSet.has(request.id) ? 'В цикле' : runtime.active[request.id] ? 'В работе' : 'Готов'}</span><IconButton title={requestBusy(request.id) ? 'Остановить запрос' : 'Запустить запрос периодически'} onClick={() => toggleRun(request.id)} disabled={operationBusy}>{requestBusy(request.id) ? <Square size={12} /> : <Play size={13} />}</IconButton></div>)}{!activeCollection.requests.length && <button className="collection-empty-add" onClick={() => setDialog({ type: 'newRequest', id: activeCollection.id })}><Plus size={18} /><span>Добавить первый запрос</span><small>HTTP, TCP, UDP или COM</small></button>}</div></div> : <div className="overview"><div className="overview-kicker"><span className="brand-mark"><Zap size={18} fill="currentColor" /></span>ОДНО ПРОСТРАНСТВО. ВСЕ ПРОТОКОЛЫ.</div><h1>От идеи до ответа.</h1><p>Проверяйте API, отправляйте байты и управляйте устройствами.<br />Все подключения, коллекции и история — под рукой.</p><div className="overview-actions"><button className="button primary" onClick={() => setDialog({ type: 'newCollection' })}><Plus size={15} />Создать коллекцию</button><button className="button secondary" onClick={() => setDialog({ type: 'import' })}><ArrowDownToLine size={15} />Импортировать</button></div><div className="overview-metrics"><div><Folder size={17} /><strong>{workspace.collections.length}</strong><span>{plural(workspace.collections.length, 'коллекция', 'коллекции', 'коллекций')}</span></div><div><FileCode2 size={17} /><strong>{requestCount}</strong><span>{plural(requestCount, 'запрос', 'запроса', 'запросов')}</span></div><div><Activity size={17} /><strong>{runtime.running.length}</strong><span>в цикле</span></div><div><Terminal size={17} /><strong>{formatNumber(history.length)}</strong><span>{plural(history.length, 'запись', 'записи', 'записей')}</span></div></div></div>}
          </div>
          {resizeHandle('editorHeight')}
          <section className="journal" aria-label="Журнал запросов"><div className="journal-heading"><div className="journal-title"><Terminal size={15} /><strong>Журнал</strong><span className="small-count">{formatNumber(scopedHistory.length)}</span>{(activeId ? activeRunning : runtime.running.length > 0) && <span className="live-label"><span className="running-dot" />LIVE</span>}</div><div className="journal-statistics"><span title="Успешные завершённые запросы"><span className="success-stat" />{successes}<span className="stat-caption">успешно</span></span><span title="Среднее время завершённых запросов"><Clock3 size={12} />{meanTime}<span className="stat-caption">мс в среднем</span></span></div><IconButton title="Очистить журнал" disabled={!scopedHistory.length} onClick={() => setDialog({ type: 'clear', id: activeId })}><Trash2 size={14} /></IconButton></div><div className="journal-toolbar"><div className="log-search"><Search size={13} /><input aria-label="Поиск в журнале" placeholder="Поиск в запросах и ответах…" value={logSearch} onChange={e => setLogSearch(e.target.value)} />{logSearch && <IconButton title="Очистить поиск журнала" onClick={() => setLogSearch('')}><X size={12} /></IconButton>}</div><div className="log-filters">{[['all', 'Все'], ['success', 'Успешные'], ['error', 'Ошибки'], ['other', 'Прочие']].map(([value, label]) => <button key={value} className={logFilter === value ? 'active' : ''} onClick={() => setLogFilter(value)}>{label}</button>)}</div><span className="journal-scope" title={activeRequest?.name ?? activeCollection?.name ?? 'Все запросы'}>{activeRequest ? 'ЗАПРОС' : activeCollection ? 'КОЛЛЕКЦИЯ' : 'ВСЕ ЗАПРОСЫ'}</span></div>
            <div className="log-table-wrap"><table className="log-table"><thead><tr><th>Время</th><th>Результат</th><th>{activeRequest ? 'Адрес' : 'Запрос / адрес'}</th><th>Время, мс</th><th /></tr></thead><tbody>{filteredHistory.slice(0, logLimit).map(entry => <tr key={entry.entryId} className={selectedLogId === entry.entryId ? 'selected' : ''} tabIndex={0} aria-selected={selectedLogId === entry.entryId} onClick={() => setSelectedLogId(entry.entryId)} onKeyDown={e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); setSelectedLogId(entry.entryId); } }}><td className="log-time">{formatTime(entry.sentAt)}<span className="log-sequence">#{entry.sequence}</span></td><td><span className={`result-pill ${resultClass(entry.result)}`}>{entry.result === 0 ? <LoaderCircle size={11} className="spin" /> : entry.result === 1 ? <Check size={11} /> : isError(entry) ? <CircleAlert size={11} /> : <Minus size={11} />}{entry.httpStatus || resultLabels[entry.result]}</span></td><td className="log-address"><Badge protocol={entry.protocol} method={entry.method} /><span title={entry.errorText || entry.url}>{!activeRequest && <strong>{entry.requestName}<span> · </span></strong>}{entry.url || entry.errorText}</span></td><td className="log-duration">{entry.result === 0 ? '…' : formatNumber(entry.elapsedMs)}</td><td className="log-arrow"><ChevronRight size={12} /></td></tr>)}</tbody></table>{!filteredHistory.length && <div className="journal-empty"><span className="empty-terminal"><Terminal size={25} /><i /></span><h3>{scopedHistory.length ? 'Совпадений не найдено' : 'Здесь появятся ответы'}</h3><p>{scopedHistory.length ? 'Измените поисковый запрос или выберите другой фильтр.' : activeRequest ? 'Отправьте запрос, чтобы увидеть результат и подробности.' : 'Запустите запрос или коллекцию, чтобы начать работу.'}</p>{!scopedHistory.length && activeRequest && <span className="shortcut-hint"><kbd>Ctrl</kbd> + <kbd>Enter</kbd><span>отправить запрос</span></span>}</div>}{filteredHistory.length > logLimit && <button className="load-more" onClick={() => setLogLimit(count => count + 200)}>Показать ещё {Math.min(200, filteredHistory.length - logLimit)} {plural(Math.min(200, filteredHistory.length - logLimit), 'запись', 'записи', 'записей')} <ChevronDown size={13} /></button>}</div>
          </section>
          {selectedLog && <>{resizeHandle('detailHeight')}<section className="log-details" style={{ height: Math.min(preferences.detailHeight, Math.max(100, window.innerHeight - 430)) }}><div className="details-heading"><div className="detail-tabs">{([['response', 'Ответ'], ['request', 'Запрос'], ['headers', 'Заголовки']] as const).map(([tab, label]) => <button key={tab} className={detailTab === tab ? 'active' : ''} onClick={() => setDetailTab(tab)}>{tab === 'response' ? <ArrowDownToLine size={13} /> : tab === 'request' ? <ArrowUpFromLine size={13} /> : <Braces size={13} />}{label}{tab === 'headers' && <span className="small-count">{selectedLog.responseHeaders.length + selectedLog.requestHeaders.length}</span>}</button>)}</div><div className="detail-meta"><span className={resultClass(selectedLog.result)}>{selectedLog.httpStatus || resultLabels[selectedLog.result]}</span><span>{formatNumber(selectedLog.elapsedMs)} мс</span><span>{formatBytes(selectedLog.responseBytes ?? new TextEncoder().encode(selectedLog.responseBody).length)}</span></div><IconButton title="Закрыть подробности" onClick={() => setSelectedLogId('')}><X size={14} /></IconButton></div>
            {selectedLog.errorText && <div className="log-error"><CircleAlert size={13} /><span>{selectedLog.errorText}</span></div>}
            {detailTab === 'headers' ? <div className="headers-detail"><HeadersView label="Заголовки ответа" headers={selectedLog.responseHeaders} /><HeadersView label="Заголовки запроса" headers={selectedLog.requestHeaders} /></div> : <><div className="response-toolbar"><div className="format-switch">{(['pretty', 'raw', 'hex'] as const).map(mode => <button className={detailMode === mode ? 'active' : ''} key={mode} onClick={() => changeDetailMode(mode)}>{mode === 'pretty' ? 'Форматировать' : mode === 'raw' ? 'Исходный' : 'HEX'}</button>)}</div><span className="body-type">{detailMode === 'hex' ? 'HEX DUMP' : (() => { try { JSON.parse(detailText); return 'JSON'; } catch { return 'TEXT'; } })()}</span><IconButton title="Копировать содержимое" disabled={!detailText} onClick={() => void copyText(detailText)}><Copy size={13} /></IconButton></div><pre className={`response-code ${detailMode === 'hex' ? 'hex-view' : ''}`}>{detailText ? <><CodeText text={detailText.slice(0, 256000)} highlight={detailMode === 'pretty' && detailText.length < 100000} />{detailText.length > 256000 && <span className="empty-response">{'\n\nПоказаны первые 256 000 символов. Кнопка копирования сохраняет полное содержимое.'}</span>}</> : <span className="empty-response">{selectedLog.result === 0 ? 'Ожидание ответа…' : detailTab === 'request' ? 'Тело запроса пустое.' : 'Тело ответа пустое.'}</span>}</pre></>}
          </section></>}
        </main>
      </div>
      <footer className="statusbar"><span className="status-brand"><span className="connection-dot" />Локальное пространство</span><span>{requestCount} {plural(requestCount, 'запрос', 'запроса', 'запросов')}</span><span className="status-spacer" />{(runtime.running.length > 0 || totalActive > 0) && <button className="stop-all" onClick={() => void act(() => window.desktop.stop(), false)}><Square size={9} fill="currentColor" />Остановить все ({runtime.running.length || totalActive})</button>}<button className={`save-state ${saveStatus}`} onClick={() => { if (saveStatus === 'error') void flushSave().catch(() => undefined); }} disabled={saveStatus !== 'error'} title={saveStatus === 'error' ? 'Повторить сохранение' : 'Изменения сохраняются автоматически'}>{saveStatus === 'saving' ? <LoaderCircle size={11} className="spin" /> : saveStatus === 'error' ? <CircleAlert size={11} /> : <Check size={11} />}{saveStatus === 'saving' ? 'Сохранение…' : saveStatus === 'error' ? 'Ошибка сохранения · повторить' : 'Всё сохранено'}</button><button className="keyboard-help" onClick={() => setShowShortcuts(value => !value)}>Горячие клавиши</button><span className="version">v{version}</span></footer>
    </>}
    <datalist id="serial-ports">{ports.map(port => <option key={port.path} value={port.path}>{port.friendlyName ?? port.manufacturer ?? port.path}</option>)}</datalist>
    {menu && <div className="menu-backdrop" onMouseDown={() => setMenu(null)}><div className="context-menu" role="menu" style={{ left: menu.x, top: menu.y }} onMouseDown={e => e.stopPropagation()}>{!lookup(menu.id).request && <button role="menuitem" onClick={() => { setDialog({ type: 'newRequest', id: menu.id }); setMenu(null); }}><Plus size={14} />Добавить запрос</button>}<button role="menuitem" onClick={() => renameDialog(menu.id)}><Pencil size={14} />Переименовать</button><button role="menuitem" onClick={() => duplicate(menu.id)}><Copy size={14} />Дублировать</button><button role="menuitem" onClick={() => { toggleRun(menu.id); setMenu(null); }}><Play size={14} />{lookup(menu.id).request ? requestBusy(menu.id) ? 'Остановить' : 'Запустить цикл' : lookup(menu.id).collection?.requests.some(r => requestBusy(r.id)) ? 'Остановить все' : 'Запустить все'}</button><div className="menu-divider" /><button className="danger-text" role="menuitem" onClick={() => { setDialog({ type: 'delete', id: menu.id }); setMenu(null); }}><Trash2 size={14} />Удалить</button></div></div>}
    {showShortcuts && <div className="shortcut-popover"><div><strong>Быстрее с клавиатурой</strong><IconButton title="Закрыть подсказку" onClick={() => setShowShortcuts(false)}><X size={13} /></IconButton></div><p><span>Отправить запрос</span><kbd>Ctrl + Enter</kbd></p><p><span>Запустить / остановить цикл</span><kbd>F5</kbd></p><p><span>Перейти к адресу / данным</span><kbd>Ctrl + L</kbd></p><p><span>Сохранить изменения</span><kbd>Ctrl + S</kbd></p></div>}
    {dialog && <Modal key={`${dialog.type}:${dialog.id ?? ''}`} dialog={dialog} name={dialog.id ? tabInfo(dialog.id)?.name ?? '' : ''} onClose={() => setDialog(null)} onSubmit={value => void submitDialog(value)} busy={dialogBusy} />}
    <div className="toast-container" aria-live="polite">{toasts.map(toast => <div className={`toast ${toast.kind}`} key={toast.id}>{toast.kind === 'error' ? <CircleAlert size={17} /> : toast.kind === 'success' ? <Check size={17} /> : <Database size={17} />}<span>{toast.message}</span><IconButton title="Закрыть сообщение" onClick={() => setToasts(items => items.filter(item => item.id !== toast.id))}><X size={14} /></IconButton></div>)}</div>
  </div>;
}

function ListIcon() { return <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round"><path d="M9 6h12M9 12h12M9 18h12M3 6h.01M3 12h.01M3 18h.01" /></svg>; }
function HeadersView({ label, headers }: { label: string; headers: Header[] }) { return <div className="header-detail-block"><h4>{label}<span>{headers.length}</span></h4>{headers.length ? <table><tbody>{headers.map((header, index) => <tr key={index}><th>{header.name}</th><td>{header.value}</td></tr>)}</tbody></table> : <p>Нет заголовков</p>}</div>; }
function CodeText({ text, highlight }: { text: string; highlight: boolean }) {
  if (!highlight) return <>{text}</>;
  let isJson = false; try { JSON.parse(text); isJson = true; } catch { /* Plain text. */ }
  if (!isJson) return <>{text}</>;
  return <>{text.split(/("(?:\\.|[^"\\])*"\s*:|"(?:\\.|[^"\\])*"|\b(?:true|false|null)\b|-?\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)/g).map((part, index) => <span key={index} className={part.startsWith('"') ? part.trimEnd().endsWith(':') ? 'json-key' : 'json-string' : /^(true|false|null)$/.test(part) ? 'json-keyword' : /^-?\d/.test(part) ? 'json-number' : undefined}>{part}</span>)}</>;
}
