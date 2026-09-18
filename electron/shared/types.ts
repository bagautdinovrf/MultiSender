export type Protocol = 'HTTP' | 'TCP' | 'UDP' | 'COM';
export interface Header { name: string; value: string; enabled?: boolean }
export interface SerialSettings { portName: string; baudRate: number; dataBits: 5|6|7|8; parity: number; stopBits: number; flowControl: number }
export interface RequestDefinition {
  id: string; name: string; protocol: Protocol; method: 'GET'|'POST'; target: string; body: string;
  waitForResponse: boolean; timeoutMs: number; serial: SerialSettings; headers: Header[];
  host: string; port: number; intervalMs: number; maxInFlight: number;
  overrideHost: boolean; overridePort: boolean; overrideIntervalMs: boolean; overrideMaxInFlight: boolean;
}
export interface Collection { id: string; name: string; host: string; port: number; intervalMs: number; maxInFlight: number; requests: RequestDefinition[] }
export interface Workspace { schemaVersion: number; collections: Collection[] }
export interface ResolvedRequest extends RequestDefinition { collectionId: string; collectionName: string }
// Numeric result values intentionally match the Qt application's history format.
export type Result = 0|1|2|3|4|5|6;
export interface LogEntry { entryId: string; sequence: string; collectionId: string; requestId: string; collectionName: string; requestName: string; protocol: Protocol; method: 'GET'|'POST'; sentAt: string; completedAt: string; result: Result; url: string; requestBody: string; responseBody: string; requestHeaders: Header[]; responseHeaders: Header[]; errorText: string; httpStatus: number; elapsedMs: number; responseHex?: string; responseBytes?: number }
export interface RuntimeState { running: string[]; active: Record<string, number> }
export interface Preferences { tabs: string[]; activeTab: string; theme: 'dark'|'light'; sidebarWidth: number; editorHeight: number; detailHeight: number; prettyJson: boolean; window?: {width: number; height: number; x?: number; y?: number; maximized?: boolean} }
export interface Snapshot { workspace: Workspace; history: LogEntry[]; preferences: Preferences; runtime: RuntimeState; version: string; dataDirectory: string; importedFrom?: string; warnings: string[] }
export interface SerialPortInfo { path: string; manufacturer?: string; friendlyName?: string }
export interface DesktopAPI {
  load(): Promise<Snapshot>;
  saveWorkspace(workspace: Workspace): Promise<void>;
  savePreferences(preferences: Preferences): Promise<void>;
  send(requestId: string): Promise<void>;
  start(id: string): Promise<void>;
  stop(id?: string): Promise<void>;
  clearHistory(id?: string): Promise<void>;
  listPorts(): Promise<SerialPortInfo[]>;
  importWorkspace(): Promise<Workspace|null>;
  exportWorkspace(): Promise<boolean>;
  openDataDirectory(): Promise<void>;
  windowAction(action: 'minimize'|'maximize'|'close'): void;
  onLog(callback: (entry: LogEntry) => void): () => void;
  onRuntime(callback: (state: RuntimeState) => void): () => void;
  onError(callback: (message: string) => void): () => void;
  onBeforeClose(callback: () => Promise<void>): () => void;
}
declare global { interface Window { desktop: DesktopAPI } }
