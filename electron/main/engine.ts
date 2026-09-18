import { randomUUID } from 'node:crypto';
import { lookup } from 'node:dns';
import { createSocket, type Socket as DatagramSocket } from 'node:dgram';
import http, { type ClientRequest, type IncomingMessage, type OutgoingHttpHeaders } from 'node:http';
import https from 'node:https';
import net from 'node:net';
import { performance } from 'node:perf_hooks';
import { createBrotliDecompress, createGunzip, createInflate } from 'node:zlib';
import type { Transform } from 'node:stream';
import { SerialPort } from 'serialport';
import type { Header, LogEntry, ResolvedRequest, Result, RuntimeState } from '../shared/types';

const MAX_RESPONSE_BYTES = 16 * 1024 * 1024;
const MAX_PAYLOAD_BYTES = 16 * 1024 * 1024;
const HTTP_TIMEOUT_MS = 30_000;
const MAX_INTERVAL_MS = 86_400_000;
const PARITY = { 0: 'none', 2: 'even', 3: 'odd', 4: 'space', 5: 'mark' } as const;
const message = (error: unknown): string => error instanceof Error ? error.message : String(error);
const serialKey = (name: string): string => name.trim().replace(/^\\\\\.\\/, '').toUpperCase();
const networkHost = (host: string): string => host.trim().replace(/^\[([\s\S]+)\]$/, '$1');
const isIntegerIn = (value: number, min: number, max: number): boolean => Number.isInteger(value) && value >= min && value <= max;

/** Parse literal UTF-8 runs as a whole, retaining UTF-16 surrogate pairs. */
export function parsePayload(text: string): Buffer {
  const sourceLength = Buffer.byteLength(text, 'utf8');
  if (sourceLength > MAX_PAYLOAD_BYTES * 4) throw new Error('Данные превышают допустимый размер 16 МиБ после преобразования');
  // Escapes never expand the UTF-8 input. A single buffer also avoids millions
  // of tiny allocations for a long sequence such as \\x00\\x00\\x00.
  const output = Buffer.allocUnsafe(Math.min(sourceLength, MAX_PAYLOAD_BYTES));
  let literalStart = 0;
  let length = 0;
  const reserve = (size: number) => {
    if (length + size > MAX_PAYLOAD_BYTES) throw new Error('Данные превышают допустимый размер 16 МиБ после преобразования');
  };
  const appendText = (literal: string) => {
    reserve(Buffer.byteLength(literal, 'utf8'));
    length += output.write(literal, length, 'utf8');
  };
  const appendByte = (value: number) => {
    reserve(1);
    output[length++] = value;
  };
  for (let position = 0; position < text.length;) {
    if (text[position] !== '\\' || position + 1 === text.length || !['\\', 'x', 'X'].includes(text[position + 1])) {
      ++position;
      continue;
    }
    appendText(text.slice(literalStart, position));
    if (text[position + 1] === '\\') {
      appendByte(0x5c);
      position += 2;
    } else {
      const hex = text.slice(position + 2, position + 4);
      if (!/^[0-9a-f]{2}$/i.test(hex)) throw new Error(`Некорректная запись байта в позиции ${position + 1}: после \\x нужны две шестнадцатеричные цифры (00–FF)`);
      appendByte(Number.parseInt(hex, 16));
      position += 4;
    }
    literalStart = position;
  }
  appendText(text.slice(literalStart));
  return output.subarray(0, length);
}

export function buildRequestUrl(config: ResolvedRequest): string {
  if (config.protocol === 'COM') {
    const serial = config.serial;
    const query = new URLSearchParams({ baud: String(serial.baudRate), data: String(serial.dataBits), parity: PARITY[serial.parity as keyof typeof PARITY] ?? 'none', stop: serial.stopBits === 3 ? '1.5' : String(serial.stopBits), flow: ['none', 'hardware', 'software'][serial.flowControl] ?? 'none' });
    return `com:/${encodeURIComponent(serialKey(serial.portName))}?${query}`;
  }
  const host = networkHost(config.host);
  const authority = `${host.includes(':') ? `[${host}]` : host}:${config.port}`;
  if (config.protocol !== 'HTTP') return `${config.protocol.toLowerCase()}://${authority}`;
  return new URL(config.target, `http://${authority}`).toString();
}

/** Empty means valid. Validation is shared by one-shot and periodic dispatch. */
export function validateConfig(config: ResolvedRequest): string {
  try {
    if (!['HTTP', 'TCP', 'UDP', 'COM'].includes(config.protocol)) return 'Неизвестный протокол';
    if (!config.id || !config.collectionId) return 'Не указан идентификатор запроса или коллекции';
    if (!isIntegerIn(config.intervalMs, 1, MAX_INTERVAL_MS)) return 'Интервал должен быть от 1 до 86 400 000 мс';
    if (config.protocol === 'COM') {
      const serial = config.serial;
      if (!/^COM[1-9][0-9]*$/i.test(serialKey(serial.portName))) return 'Укажите имя COM-порта, например COM3';
      if (!isIntegerIn(serial.baudRate, 1, 2_147_483_647)) return 'Скорость COM-порта должна быть целым числом больше нуля';
      if (![5, 6, 7, 8].includes(serial.dataBits)) return 'Укажите от 5 до 8 бит данных';
      if (!Object.hasOwn(PARITY, serial.parity)) return 'Неверная настройка чётности COM-порта';
      if (![1, 2, 3].includes(serial.stopBits)) return 'Укажите 1, 1.5 или 2 стоп-бита';
      if ((serial.stopBits === 3 && serial.dataBits !== 5) || (serial.stopBits === 2 && serial.dataBits === 5)) return 'При 5 битах данных допустимы 1 или 1.5 стоп-бита; при 6–8 — 1 или 2';
      if (![0, 1, 2].includes(serial.flowControl)) return 'Неверная настройка управления потоком COM-порта';
    } else {
      const host = networkHost(config.host);
      if (!host) return 'Укажите адрес сервера';
      if (/[\s/@?#\\]/.test(host) || (host.includes(':') && !net.isIP(host))) return 'В адресе укажите только имя хоста или IP без схемы и пути';
      if (!isIntegerIn(config.port, 1, 65535)) return 'Порт должен быть от 1 до 65535';
      if (!isIntegerIn(config.maxInFlight, 1, 10_000)) return 'Лимит параллельных запросов должен быть от 1 до 10 000';
    }
    if (config.protocol === 'HTTP') {
      if (!['GET', 'POST'].includes(config.method)) return 'Поддерживаются HTTP-методы GET и POST';
      if (!config.target.startsWith('/') || config.target.startsWith('//')) return 'Путь должен начинаться с одного символа /';
      if (/[#\\\u0000-\u0020\u007f]/.test(config.target) || /%(?![\da-f]{2})/i.test(config.target)) return 'Путь или query-параметры имеют неверный формат: используйте URL-кодирование';
      if (Buffer.byteLength(config.body, 'utf8') > MAX_PAYLOAD_BYTES) return 'Тело HTTP-запроса превышает 16 МиБ';
      const names = new Set<string>();
      for (const header of config.headers.filter(header => header.enabled !== false)) {
        http.validateHeaderName(header.name);
        http.validateHeaderValue(header.name, header.value);
        const lower = header.name.toLowerCase();
        if (['host', 'content-length', 'transfer-encoding'].includes(lower) && names.has(lower)) return `Заголовок ${header.name} не должен повторяться`;
        names.add(lower);
        if (lower === 'content-length' && (!/^\d+$/.test(header.value) || Number(header.value) !== (config.method === 'POST' ? Buffer.byteLength(config.body, 'utf8') : 0))) return 'Content-Length должен совпадать с размером отправляемого тела в байтах';
        if (lower === 'transfer-encoding' && (header.value.trim().toLowerCase() !== 'chunked' || config.method !== 'POST')) return 'Transfer-Encoding поддерживается только со значением chunked для POST';
      }
      if (names.has('content-length') && names.has('transfer-encoding')) return 'Нельзя одновременно использовать Content-Length и Transfer-Encoding';
    } else {
      if (!isIntegerIn(config.timeoutMs, 1, MAX_INTERVAL_MS)) return 'Тайм-аут должен быть от 1 до 86 400 000 мс';
      const payload = parsePayload(config.body);
      if (config.protocol === 'UDP' && payload.length > 65_507) return 'Данные UDP не должны превышать 65 507 байт после преобразования';
    }
    buildRequestUrl(config);
    return '';
  } catch (error) {
    return message(error);
  }
}

interface Operation {
  config: ResolvedRequest;
  entry: LogEntry;
  started: number;
  done: boolean;
  phase: 'connecting' | 'writing' | 'reading';
  chunks: Buffer[];
  responseBytes: number;
  timer?: ReturnType<typeof setTimeout>;
  cleanup: () => void;
}
interface Periodic { config: ResolvedRequest; timer?: ReturnType<typeof setInterval> }

export class RequestEngine {
  private readonly active = new Map<string, Operation>();
  private readonly periodic = new Map<string, Periodic>();
  private readonly serialReservations = new Set<string>();
  private nextSequence = 1n;

  constructor(private readonly onLog: (entry: LogEntry) => void, private readonly onRuntime: (runtime: RuntimeState) => void, private readonly userAgent = 'MultiSenderElectron') {}

  restoreSequence(history: readonly LogEntry[]): void {
    for (const entry of history) {
      if (/^\d+$/.test(entry.sequence)) {
        const value = BigInt(entry.sequence);
        if (value >= this.nextSequence) this.nextSequence = value + 1n;
      }
    }
  }

  runtime(): RuntimeState {
    const active: Record<string, number> = Object.create(null) as Record<string, number>;
    for (const operation of this.active.values()) active[operation.config.id] = (active[operation.config.id] ?? 0) + 1;
    return { running: [...this.periodic.keys()], active };
  }

  send(config: ResolvedRequest): void { this.dispatch(structuredClone(config)); }

  start(config: ResolvedRequest): void {
    if (this.periodic.has(config.id)) return;
    const snapshot = structuredClone(config);
    if (validateConfig(snapshot)) { this.dispatch(snapshot); return; }
    const periodic: Periodic = { config: snapshot };
    this.periodic.set(snapshot.id, periodic);
    this.onRuntime(this.runtime());
    this.dispatch(snapshot);
    // A synchronous observer is allowed to stop a newly started request.
    if (this.periodic.get(snapshot.id) === periodic) periodic.timer = setInterval(() => this.dispatch(snapshot), snapshot.intervalMs);
  }

  stop(id?: string): void {
    const matches = (config: ResolvedRequest) => !id || config.id === id || config.collectionId === id;
    for (const [key, periodic] of this.periodic) {
      if (!matches(periodic.config)) continue;
      clearInterval(periodic.timer);
      this.periodic.delete(key);
    }
    for (const operation of [...this.active.values()]) if (matches(operation.config)) this.finish(operation, 4, 'Запрос отменён пользователем');
    this.onRuntime(this.runtime());
  }

  private dispatch(config: ResolvedRequest): void {
    const entry: LogEntry = {
      entryId: randomUUID(), sequence: String(this.nextSequence++), collectionId: config.collectionId, requestId: config.id,
      collectionName: config.collectionName, requestName: config.name, protocol: config.protocol, method: config.method,
      sentAt: new Date().toISOString(), completedAt: '', result: 0, url: '', requestBody: config.protocol === 'HTTP' && config.method === 'GET' ? '' : config.body,
      responseBody: '', requestHeaders: [], responseHeaders: [], errorText: '', httpStatus: 0, elapsedMs: 0,
    };
    const validation = validateConfig(config);
    try { entry.url = buildRequestUrl(config); } catch { entry.url = config.host ?? ''; }
    let skipReason = '';
    const sameRequest = [...this.active.values()].filter(operation => operation.config.id === config.id);
    if (!validation && config.protocol === 'COM') {
      if (this.serialReservations.has(`${config.id}:${serialKey(config.serial.portName)}`) || sameRequest.some(operation => operation.config.protocol === 'COM' && serialKey(operation.config.serial.portName) === serialKey(config.serial.portName))) skipReason = `Порт ${serialKey(config.serial.portName)} занят предыдущим запросом; дождитесь его завершения`;
    } else if (!validation && sameRequest.length >= config.maxInFlight) skipReason = `Достигнут лимит параллельных запросов: ${config.maxInFlight}`;
    if (validation || skipReason) {
      this.onLog({ ...entry });
      entry.completedAt = new Date().toISOString();
      entry.result = skipReason ? 5 : config.protocol === 'COM' ? 6 : 3;
      entry.errorText = skipReason || validation;
      this.onLog(entry);
      return;
    }
    const operation: Operation = { config, entry, started: performance.now(), done: false, phase: 'connecting', chunks: [], responseBytes: 0, cleanup: () => {} };
    this.active.set(entry.entryId, operation);
    this.onLog({ ...entry });
    this.onRuntime(this.runtime());
    queueMicrotask(() => {
      if (operation.done) return;
      try {
        if (config.protocol === 'HTTP') this.http(operation);
        else if (config.protocol === 'TCP') this.tcp(operation);
        else if (config.protocol === 'UDP') this.udp(operation);
        else this.serial(operation);
      } catch (error) { this.finish(operation, config.protocol === 'COM' ? 6 : 3, message(error)); }
    });
  }

  private arm(operation: Operation, timeoutMs: number, httpTimeout = false): void {
    if (operation.done) return;
    clearTimeout(operation.timer);
    operation.timer = setTimeout(() => {
      if (!httpTimeout && operation.phase === 'reading' && operation.responseBytes > 0) this.finish(operation, 1);
      else this.finish(operation, operation.config.protocol === 'COM' ? 6 : 3, `Истекло время ${operation.phase === 'reading' ? 'ожидания ответа' : 'подключения или отправки'} (${timeoutMs} мс)`);
    }, timeoutMs);
  }

  private append(operation: Operation, data: Buffer): boolean {
    if (operation.done) return false;
    if (operation.responseBytes + data.length > MAX_RESPONSE_BYTES) {
      this.finish(operation, operation.config.protocol === 'COM' ? 6 : 3, 'Ответ превышает ограничение 16 МиБ; получение остановлено');
      return false;
    }
    operation.chunks.push(data);
    operation.responseBytes += data.length;
    return true;
  }

  private finish(operation: Operation, result: Result, errorText = ''): void {
    if (operation.done) return;
    operation.done = true;
    clearTimeout(operation.timer);
    this.active.delete(operation.entry.entryId);
    // Mark done before closing: Node streams can emit error/close synchronously.
    try { operation.cleanup(); } catch { /* Already closed resources are harmless. */ }
    const response = Buffer.concat(operation.chunks, operation.responseBytes);
    operation.chunks = [];
    const entry: LogEntry = { ...operation.entry, result, errorText, completedAt: new Date().toISOString(), elapsedMs: Math.max(0, Math.round(performance.now() - operation.started)), responseBody: response.toString('utf8'), responseBytes: response.length };
    if (operation.config.protocol !== 'HTTP') entry.responseHex = response.toString('hex');
    this.onLog(entry);
    this.onRuntime(this.runtime());
  }

  private sent(operation: Operation): void {
    if (operation.done) return;
    if (!operation.config.waitForResponse) { this.finish(operation, 1); return; }
    operation.phase = 'reading';
    this.arm(operation, operation.config.timeoutMs);
  }

  private tcp(operation: Operation): void {
    const config = operation.config;
    const payload = parsePayload(config.body);
    const socket = new net.Socket();
    operation.cleanup = () => { socket.destroy(); };
    socket.on('error', error => this.finish(operation, 3, message(error)));
    socket.on('data', data => this.append(operation, data));
    socket.on('end', () => {
      if (operation.done) return;
      if (operation.phase === 'reading' && operation.responseBytes > 0) this.finish(operation, 1);
      else this.finish(operation, 3, operation.phase === 'reading' ? 'Сервер закрыл соединение без ответа' : 'Соединение закрыто до завершения отправки');
    });
    socket.on('close', () => {
      if (!operation.done) this.finish(operation, 3, 'Соединение закрыто до завершения запроса');
    });
    this.arm(operation, config.timeoutMs);
    socket.connect(config.port, networkHost(config.host), () => {
      if (operation.done) return;
      operation.phase = 'writing';
      // The callback acknowledges the entire write, not just enqueueing it.
      socket.write(payload, error => error ? this.finish(operation, 3, message(error)) : this.sent(operation));
    });
  }

  private udp(operation: Operation): void {
    const config = operation.config;
    const payload = parsePayload(config.body);
    let socket: DatagramSocket | undefined;
    operation.cleanup = () => { if (socket) { try { socket.close(); } catch { /* DNS may finish after cancellation. */ } } };
    this.arm(operation, config.timeoutMs);
    // DNS cannot be interrupted in Node; its callback must never open a cancelled socket.
    lookup(networkHost(config.host), (error, address, family) => {
      if (operation.done) return;
      if (error) { this.finish(operation, 3, message(error)); return; }
      try {
        socket = createSocket(family === 6 ? 'udp6' : 'udp4');
        socket.on('error', failure => this.finish(operation, 3, message(failure)));
        let earlyResponse: Buffer | undefined;
        socket.on('message', data => {
          if (operation.done || !config.waitForResponse) return;
          if (operation.phase !== 'reading') { earlyResponse ??= data; return; }
          if (this.append(operation, data)) this.finish(operation, 1);
        });
        // A connected UDP socket filters datagrams by resolved peer address + port.
        socket.connect(config.port, address, () => {
          if (operation.done || !socket) return;
          operation.phase = 'writing';
          socket.send(payload, failure => {
            if (operation.done) return;
            if (failure) { this.finish(operation, 3, message(failure)); return; }
            this.sent(operation);
            if (!operation.done && earlyResponse !== undefined && this.append(operation, earlyResponse)) this.finish(operation, 1);
          });
        });
      } catch (failure) { this.finish(operation, 3, message(failure)); }
    });
  }

  private serial(operation: Operation): void {
    const config = operation.config;
    const settings = config.serial;
    const payload = parsePayload(config.body);
    const port = new SerialPort({
      path: serialKey(settings.portName), baudRate: settings.baudRate, dataBits: settings.dataBits,
      parity: PARITY[settings.parity as keyof typeof PARITY], stopBits: settings.stopBits === 3 ? 1.5 : settings.stopBits as 1 | 2,
      rtscts: settings.flowControl === 1, xon: settings.flowControl === 2, xoff: settings.flowControl === 2, autoOpen: false,
    });
    const reservation = `${config.id}:${serialKey(settings.portName)}`;
    this.serialReservations.add(reservation);
    let opening = false;
    let closing = false;
    const release = () => this.serialReservations.delete(reservation);
    const close = () => {
      if (closing) return;
      if (port.isOpen) {
        closing = true;
        port.close(() => { closing = false; release(); });
      } else if (!opening) release();
    };
    operation.cleanup = close;
    port.on('error', error => this.finish(operation, 6, `Ошибка COM-порта ${settings.portName}: ${message(error)}`));
    port.on('data', (data: Buffer) => this.append(operation, data));
    port.on('close', () => { release(); if (!operation.done) this.finish(operation, 6, `Порт ${settings.portName} закрыт или отключён до завершения запроса`); });
    this.arm(operation, config.timeoutMs);
    opening = true;
    port.open(error => {
      opening = false;
      // If stop/timeout happened during open(), release the eventual native handle.
      if (operation.done) { close(); return; }
      if (error) { this.finish(operation, 6, `Не удалось открыть порт ${settings.portName}: ${message(error)}`); return; }
      operation.phase = 'writing';
      port.write(payload, writeError => {
        if (operation.done) return;
        if (writeError) { this.finish(operation, 6, message(writeError)); return; }
        port.drain(drainError => {
          if (operation.done) return;
          if (drainError) this.finish(operation, 6, message(drainError));
          else this.sent(operation);
        });
      });
    });
  }

  private http(operation: Operation): void {
    const config = operation.config;
    let request: ClientRequest | undefined;
    let response: IncomingMessage | undefined;
    let decoder: Transform | undefined;
    operation.cleanup = () => { decoder?.destroy(); response?.destroy(); request?.destroy(); };
    const fail = (error: unknown) => this.finish(operation, 3, message(error));
    const headers: OutgoingHttpHeaders = { 'User-Agent': this.userAgent, Connection: 'close', 'Accept-Encoding': 'gzip, deflate, br' };
    if (config.method === 'POST') headers['Content-Type'] = 'application/json';
    // First custom value overrides defaults; subsequent values keep duplicates.
    const customNames = new Set<string>();
    for (const header of config.headers.filter(header => header.enabled !== false)) {
      const name = header.name.toLowerCase();
      const old = Object.keys(headers).find(key => key.toLowerCase() === name);
      if (!customNames.has(name)) { if (old) delete headers[old]; headers[header.name] = header.value; customNames.add(name); }
      else if (old) { const value = headers[old]; headers[old] = [...(Array.isArray(value) ? value : [String(value)]), header.value]; }
    }
    const removeHeader = (name: string) => { for (const key of Object.keys(headers)) if (key.toLowerCase() === name) delete headers[key]; };
    const hasHeader = (name: string) => Object.keys(headers).some(key => key.toLowerCase() === name);
    let body = config.method === 'POST' ? Buffer.from(config.body, 'utf8') : undefined;
    let method: 'GET' | 'POST' = config.method;
    if (body && !hasHeader('content-length') && !hasHeader('transfer-encoding')) headers['Content-Length'] = body.length;
    const perform = (url: URL, redirects: number) => {
      if (operation.done) return;
      operation.phase = 'connecting';
      this.arm(operation, HTTP_TIMEOUT_MS, true);
      const transport = url.protocol === 'https:' ? https : http;
      request = transport.request(url, { method, headers, agent: false }, incoming => {
        if (operation.done) { incoming.destroy(); return; }
        response = incoming;
        incoming.on('error', fail);
        this.arm(operation, HTTP_TIMEOUT_MS, true);
        const status = incoming.statusCode ?? 0;
        const location = incoming.headers.location;
        if ([301, 302, 303, 307, 308].includes(status) && location) {
          try {
            if (redirects >= 10) throw new Error('Превышен лимит HTTP-перенаправлений (10)');
            const target = new URL(location, url);
            if (!['http:', 'https:'].includes(target.protocol) || (url.protocol === 'https:' && target.protocol === 'http:')) throw new Error('Сервер предложил небезопасное или неподдерживаемое перенаправление');
            if (url.origin !== target.origin) { removeHeader('authorization'); removeHeader('cookie'); removeHeader('proxy-authorization'); }
            removeHeader('host');
            if (status === 303 || ((status === 301 || status === 302) && method === 'POST')) {
              method = 'GET'; body = undefined; removeHeader('content-length'); removeHeader('content-type'); removeHeader('transfer-encoding');
            }
            incoming.destroy();
            perform(target, redirects + 1);
          } catch (error) { fail(error); }
          return;
        }
        operation.phase = 'reading';
        operation.entry.httpStatus = status;
        operation.entry.responseHeaders = [];
        for (let i = 0; i < incoming.rawHeaders.length; i += 2) operation.entry.responseHeaders.push({ name: incoming.rawHeaders[i], value: incoming.rawHeaders[i + 1] });
        incoming.on('aborted', () => fail(new Error('Сервер прервал HTTP-ответ')));
        incoming.on('data', () => this.arm(operation, HTTP_TIMEOUT_MS, true));
        const encoding = incoming.headers['content-encoding']?.toLowerCase().trim();
        decoder = encoding === 'gzip' ? createGunzip() : encoding === 'deflate' ? createInflate() : encoding === 'br' ? createBrotliDecompress() : undefined;
        const source = decoder ?? incoming;
        if (decoder) { decoder.on('error', fail); incoming.pipe(decoder); }
        source.on('data', (chunk: Buffer) => this.append(operation, chunk));
        source.on('end', () => this.finish(operation, status >= 400 ? 2 : 1, status >= 400 ? `HTTP ${status}${incoming.statusMessage ? ` · ${incoming.statusMessage}` : ''}` : ''));
      });
      request.on('error', fail);
      operation.phase = 'writing';
      request.end(body);
      const transmitted: Header[] = [];
      for (const name of request.getRawHeaderNames()) {
        const value = request.getHeader(name);
        if (value === undefined) continue;
        const values = Array.isArray(value) ? (name.toLowerCase() === 'cookie' ? [value.join('; ')] : value) : [String(value)];
        for (const item of values) transmitted.push({ name, value: String(item) });
      }
      // The log's URL/method/body describe the original request, so retain its
      // wire headers as well when a redirect changes the method or authority.
      if (redirects === 0) {
        operation.entry.requestHeaders = transmitted;
        this.onLog({ ...operation.entry });
      }
    };
    perform(new URL(operation.entry.url), 0);
  }
}
