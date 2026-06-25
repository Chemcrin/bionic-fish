import { webSocket } from '@kit.NetworkKit';
import { BusinessError } from '@kit.BasicServicesKit';
import {
  buildConfigFrame,
  buildControlFrame,
  buildHeartbeatFrame,
  ConnectionState,
  FishCommand,
  FishConfig,
  FishTelemetry,
  parseAckFrame,
  parseTelemetryFrame
} from '../model/FishProtocol';

export interface FishSocketEvents {
  onStateChange?: (state: ConnectionState, message: string) => void;
  onTelemetry?: (telemetry: FishTelemetry) => void;
  onAck?: (message: string) => void;
  onRawMessage?: (payload: string) => void;
}

type WebSocketConnection = webSocket.WebSocket;

export class FishSocketClient {
  private socket?: WebSocketConnection;
  private endpoint: string = '';
  private seq: number = 1;
  private heartbeatTimer: number = -1;
  private controlTimer: number = -1;
  private latestCommand?: FishCommand;
  private events: FishSocketEvents;
  private connected: boolean = false;

  constructor(events: FishSocketEvents) {
    this.events = events;
  }

  connect(endpoint: string): void {
    this.disconnect(false);
    this.endpoint = endpoint;
    this.emitState('connecting', `正在连接 ${endpoint}`);

    const socket = webSocket.createWebSocket();
    this.socket = socket;

    socket.on('open', (err: BusinessError) => {
      if (err) {
        this.emitState('error', err.message);
        return;
      }
      this.connected = true;
      this.emitState('connected', 'ESP32 WebSocket 已连接');
      this.startHeartbeat();
      this.startControlLoop();
      if (this.latestCommand) {
        this.sendControl(this.latestCommand);
      }
    });

    socket.on('message', (err: BusinessError, value: string | ArrayBuffer) => {
      if (err) {
        this.emitState('error', err.message);
        return;
      }
      const payload = typeof value === 'string' ? value : this.arrayBufferToText(value);
      this.events.onRawMessage?.(payload);

      const telemetry = parseTelemetryFrame(payload);
      if (telemetry) {
        this.events.onTelemetry?.(telemetry);
        return;
      }

      const ack = parseAckFrame(payload);
      if (ack) {
        this.events.onAck?.(ack.ok ? (ack.message ?? `ACK #${ack.seq}`) : (ack.message ?? `ERR #${ack.seq}`));
      }
    });

    socket.on('close', (err: BusinessError, value: webSocket.CloseResult) => {
      this.connected = false;
      this.stopTimers();
      const reason = value?.reason ? `${value.reason}` : '连接已关闭';
      this.emitState(err ? 'error' : 'closed', err?.message ?? reason);
    });

    socket.on('error', (err: BusinessError) => {
      this.connected = false;
      this.stopTimers();
      this.emitState('error', err.message);
    });

    socket.connect(endpoint, {
      header: {
        'X-Client': 'HarmonyOS-BionicFish'
      }
    }).catch((err: BusinessError) => {
      this.connected = false;
      this.stopTimers();
      this.emitState('error', `连接请求失败：${err.message}`);
    });
  }

  disconnect(notify: boolean = true): void {
    this.stopTimers();
    this.connected = false;
    if (this.socket) {
      try {
        this.socket.close().catch((err: BusinessError) => {
          this.emitState('error', `断开失败：${err.message}`);
        });
      } catch (err) {
      }
      this.socket = undefined;
    }
    if (notify) {
      this.emitState('closed', '连接已断开');
    }
  }

  updateCommand(command: FishCommand): void {
    this.latestCommand = { ...command };
    this.sendControl(command);
  }

  sendControl(command: FishCommand): void {
    this.latestCommand = { ...command };
    if (!this.connected || !this.socket) {
      return;
    }
    const frame = buildControlFrame(this.nextSeq(), command);
    this.sendJson(frame);
  }

  sendConfig(config: FishConfig): void {
    if (!this.connected || !this.socket) {
      this.emitState('closed', '未连接，参数未发送');
      return;
    }
    this.sendJson(buildConfigFrame(this.nextSeq(), config));
  }

  emergencyStop(): void {
    const command: FishCommand = {
      mode: 'stop',
      throttle: 0,
      yaw: 0,
      pitch: 0,
      depthTarget: 0,
      tailAmp: 0,
      tailFreq: 0,
      trim: 0,
      emergencyStop: true
    };
    this.latestCommand = command;
    this.sendControl(command);
  }

  private startHeartbeat(): void {
    if (this.heartbeatTimer >= 0) {
      clearInterval(this.heartbeatTimer);
    }
    this.heartbeatTimer = setInterval(() => {
      if (this.connected && this.socket) {
        this.sendJson(buildHeartbeatFrame(this.nextSeq()));
      }
    }, 1000);
  }

  private startControlLoop(): void {
    if (this.controlTimer >= 0) {
      clearInterval(this.controlTimer);
    }
    this.controlTimer = setInterval(() => {
      if (this.latestCommand && this.connected && this.socket) {
        this.sendControl(this.latestCommand);
      }
    }, 120);
  }

  private stopTimers(): void {
    if (this.heartbeatTimer >= 0) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = -1;
    }
    if (this.controlTimer >= 0) {
      clearInterval(this.controlTimer);
      this.controlTimer = -1;
    }
  }

  private sendJson(frame: object): void {
    if (!this.socket) {
      return;
    }
    try {
      this.socket.send(JSON.stringify(frame)).catch((err: BusinessError) => {
        this.emitState('error', `发送失败：${err.message}`);
      });
    } catch (err) {
      this.emitState('error', `${err}`);
    }
  }

  private nextSeq(): number {
    this.seq += 1;
    if (this.seq > 65535) {
      this.seq = 1;
    }
    return this.seq;
  }

  private emitState(state: ConnectionState, message: string): void {
    this.events.onStateChange?.(state, message);
  }

  private arrayBufferToText(buffer: ArrayBuffer): string {
    const bytes = new Uint8Array(buffer);
    let text = '';
    for (let i = 0; i < bytes.length; i++) {
      text += String.fromCharCode(bytes[i]);
    }
    return text;
  }
}
