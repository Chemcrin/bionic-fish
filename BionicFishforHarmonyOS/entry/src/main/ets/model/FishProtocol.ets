export type FishMode = 'stop' | 'manual' | 'cruise' | 'debug';
export type ConnectionState = 'idle' | 'connecting' | 'connected' | 'closed' | 'error';

export interface ControlFrame {
  type: 'control';
  seq: number;
  mode: FishMode;
  throttle: number;
  yaw: number;
  pitch: number;
  depth_target?: number;
  tail_amp?: number;
  tail_freq?: number;
  trim?: number;
  emergency_stop: boolean;
  timestamp: number;
}

export interface ConfigFrame {
  type: 'config';
  seq: number;
  tail_amp_limit: number;
  tail_freq_limit: number;
  servo_trim_tail: number;
  servo_trim_left_fin: number;
  servo_trim_right_fin: number;
  timestamp: number;
}

export interface HeartbeatFrame {
  type: 'heartbeat';
  seq: number;
  timestamp: number;
}

export interface TelemetryFrame {
  type: 'telemetry';
  seq: number;
  battery: number;
  rssi: number;
  mode: FishMode;
  depth: number;
  roll: number;
  pitch: number;
  yaw: number;
  stm32_online: boolean;
  error: number;
}

export interface AckFrame {
  type: 'ack';
  seq: number;
  ok: boolean;
  error?: number;
  message?: string;
}

export interface FishCommand {
  mode: FishMode;
  throttle: number;
  yaw: number;
  pitch: number;
  depthTarget: number;
  tailAmp: number;
  tailFreq: number;
  trim: number;
  emergencyStop: boolean;
}

export interface FishConfig {
  tailAmpLimit: number;
  tailFreqLimit: number;
  servoTrimTail: number;
  servoTrimLeftFin: number;
  servoTrimRightFin: number;
}

export interface FishTelemetry {
  battery: number;
  rssi: number;
  mode: FishMode;
  depth: number;
  roll: number;
  pitch: number;
  yaw: number;
  stm32Online: boolean;
  error: number;
  updatedAt: number;
}

export interface CommandPreset {
  name: string;
  icon: string;
  command: FishCommand;
}

export const DEFAULT_ENDPOINT: string = 'ws://192.168.4.1/ws';

export const DEFAULT_COMMAND: FishCommand = {
  mode: 'stop',
  throttle: 0,
  yaw: 0,
  pitch: 0,
  depthTarget: 0.30,
  tailAmp: 45,
  tailFreq: 1.6,
  trim: 0,
  emergencyStop: false
};

export const DEFAULT_CONFIG: FishConfig = {
  tailAmpLimit: 80,
  tailFreqLimit: 3.5,
  servoTrimTail: 0,
  servoTrimLeftFin: -2,
  servoTrimRightFin: 2
};

export const DEFAULT_TELEMETRY: FishTelemetry = {
  battery: 0,
  rssi: 0,
  mode: 'stop',
  depth: 0,
  roll: 0,
  pitch: 0,
  yaw: 0,
  stm32Online: false,
  error: 0,
  updatedAt: 0
};

export function clamp(value: number, min: number, max: number): number {
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

export function buildControlFrame(seq: number, command: FishCommand): ControlFrame {
  return {
    type: 'control',
    seq,
    mode: command.emergencyStop ? 'stop' : command.mode,
    throttle: Math.round(clamp(command.throttle, 0, 100)),
    yaw: Math.round(clamp(command.yaw, -100, 100)),
    pitch: Math.round(clamp(command.pitch, -100, 100)),
    depth_target: Number(clamp(command.depthTarget, 0, 3).toFixed(2)),
    tail_amp: Math.round(clamp(command.tailAmp, 0, 100)),
    tail_freq: Number(clamp(command.tailFreq, 0, 5).toFixed(1)),
    trim: Math.round(clamp(command.trim, -20, 20)),
    emergency_stop: command.emergencyStop,
    timestamp: Date.now()
  };
}

export function buildConfigFrame(seq: number, config: FishConfig): ConfigFrame {
  return {
    type: 'config',
    seq,
    tail_amp_limit: Math.round(clamp(config.tailAmpLimit, 0, 100)),
    tail_freq_limit: Number(clamp(config.tailFreqLimit, 0, 5).toFixed(1)),
    servo_trim_tail: Math.round(clamp(config.servoTrimTail, -20, 20)),
    servo_trim_left_fin: Math.round(clamp(config.servoTrimLeftFin, -20, 20)),
    servo_trim_right_fin: Math.round(clamp(config.servoTrimRightFin, -20, 20)),
    timestamp: Date.now()
  };
}

export function buildHeartbeatFrame(seq: number): HeartbeatFrame {
  return {
    type: 'heartbeat',
    seq,
    timestamp: Date.now()
  };
}

export function parseTelemetryFrame(payload: string): FishTelemetry | undefined {
  try {
    const frame = JSON.parse(payload) as TelemetryFrame | AckFrame;
    if (frame.type !== 'telemetry') {
      return undefined;
    }

    return {
      battery: Number(frame.battery ?? 0),
      rssi: Number(frame.rssi ?? 0),
      mode: frame.mode ?? 'stop',
      depth: Number(frame.depth ?? 0),
      roll: Number(frame.roll ?? 0),
      pitch: Number(frame.pitch ?? 0),
      yaw: Number(frame.yaw ?? 0),
      stm32Online: Boolean(frame.stm32_online),
      error: Number(frame.error ?? 0),
      updatedAt: Date.now()
    };
  } catch (err) {
    return undefined;
  }
}

export function parseAckFrame(payload: string): AckFrame | undefined {
  try {
    const frame = JSON.parse(payload) as AckFrame;
    if (frame.type !== 'ack') {
      return undefined;
    }
    return frame;
  } catch (err) {
    return undefined;
  }
}

export const PRESETS: CommandPreset[] = [
  {
    name: '悬停',
    icon: 'pause',
    command: {
      ...DEFAULT_COMMAND,
      mode: 'manual',
      throttle: 0,
      yaw: 0,
      pitch: 0
    }
  },
  {
    name: '巡航',
    icon: 'navigation',
    command: {
      ...DEFAULT_COMMAND,
      mode: 'cruise',
      throttle: 42,
      yaw: 0,
      pitch: 0,
      tailAmp: 55,
      tailFreq: 1.8
    }
  },
  {
    name: '左转',
    icon: 'undo',
    command: {
      ...DEFAULT_COMMAND,
      mode: 'manual',
      throttle: 38,
      yaw: -48,
      pitch: 0
    }
  },
  {
    name: '右转',
    icon: 'redo',
    command: {
      ...DEFAULT_COMMAND,
      mode: 'manual',
      throttle: 38,
      yaw: 48,
      pitch: 0
    }
  },
  {
    name: '上浮',
    icon: 'arrow-up',
    command: {
      ...DEFAULT_COMMAND,
      mode: 'manual',
      throttle: 28,
      yaw: 0,
      pitch: 42
    }
  },
  {
    name: '下潜',
    icon: 'arrow-down',
    command: {
      ...DEFAULT_COMMAND,
      mode: 'manual',
      throttle: 28,
      yaw: 0,
      pitch: -42
    }
  }
];
