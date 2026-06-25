import { FishSocketClient } from '../service/FishSocketClient';
import {
  clamp,
  ConnectionState,
  DEFAULT_COMMAND,
  DEFAULT_CONFIG,
  DEFAULT_ENDPOINT,
  DEFAULT_TELEMETRY,
  FishCommand,
  FishConfig,
  FishMode,
  FishTelemetry,
  PRESETS,
  CommandPreset
} from '../model/FishProtocol';

const PANEL_BG: ResourceColor = '#33FFFFFF';
const PANEL_STROKE: ResourceColor = '#40FFFFFF';
const TEXT_MAIN: ResourceColor = '#F3FFFC';
const TEXT_MUTED: ResourceColor = '#A8C4C9';
const CYAN: ResourceColor = '#38E7CA';
const BLUE: ResourceColor = '#39A8FF';
const VIOLET: ResourceColor = '#B9A4FF';
const WARNING: ResourceColor = '#FF6B63';
const DARK: ResourceColor = '#071317';

@Entry
@Component
struct Index {
  private client?: FishSocketClient;
  private pulseTimer: number = -1;

  @State endpoint: string = DEFAULT_ENDPOINT;
  @State connectionState: ConnectionState = 'idle';
  @State statusMessage: string = '等待连接 ESP32-S3';
  @State ackMessage: string = '暂无应答';
  @State telemetry: FishTelemetry = { ...DEFAULT_TELEMETRY };
  @State command: FishCommand = { ...DEFAULT_COMMAND };
  @State config: FishConfig = { ...DEFAULT_CONFIG };
  @State selectedMode: FishMode = 'manual';
  @State isPulsing: boolean = false;
  @State rawMessage: string = '';

  aboutToAppear(): void {
    this.client = new FishSocketClient({
      onStateChange: (state: ConnectionState, message: string) => {
        this.connectionState = state;
        this.statusMessage = message;
      },
      onTelemetry: (telemetry: FishTelemetry) => {
        this.telemetry = telemetry;
      },
      onAck: (message: string) => {
        this.ackMessage = message;
      },
      onRawMessage: (payload: string) => {
        this.rawMessage = payload;
      }
    });

    this.pulseTimer = setInterval(() => {
      animateTo({ duration: 1600, curve: Curve.EaseInOut }, () => {
        this.isPulsing = !this.isPulsing;
      });
    }, 1600);
  }

  aboutToDisappear(): void {
    if (this.pulseTimer >= 0) {
      clearInterval(this.pulseTimer);
      this.pulseTimer = -1;
    }
    this.client?.disconnect(false);
  }

  build() {
    Stack({ alignContent: Alignment.TopStart }) {
      this.WaterLightBackground();

      Scroll() {
        Column({ space: 18 }) {
          this.TopBar();
          this.HeroConsole();
          this.ModeTabs();
          this.ControlPad();
          this.TelemetryGrid();
          this.ParameterDeck();
          this.RawConsole();
        }
        .width('100%')
        .padding({ left: 18, right: 18, top: 16, bottom: 28 })
      }
      .width('100%')
      .height('100%')
      .scrollBar(BarState.Off)
    }
    .width('100%')
    .height('100%')
    .backgroundColor(DARK)
  }

  @Builder
  WaterLightBackground() {
    Stack() {
      Column()
        .width('100%')
        .height('100%')
        .linearGradient({
          angle: 156,
          colors: [
            ['#071317', 0],
            ['#0A2C34', 0.42],
            ['#11233E', 0.72],
            ['#0A0F17', 1]
          ]
        })

      Circle()
        .width(this.isPulsing ? 360 : 300)
        .height(this.isPulsing ? 360 : 300)
        .fill('#224FE7CA')
        .blur(44)
        .position({ x: -88, y: 34 })
        .opacity(this.isPulsing ? 0.82 : 0.52)

      Circle()
        .width(this.isPulsing ? 240 : 300)
        .height(this.isPulsing ? 240 : 300)
        .fill('#2639A8FF')
        .blur(54)
        .position({ x: 220, y: 100 })
        .opacity(this.isPulsing ? 0.46 : 0.68)

      Circle()
        .width(260)
        .height(260)
        .fill('#22B9A4FF')
        .blur(56)
        .position({ x: 80, y: 590 })
        .opacity(this.isPulsing ? 0.52 : 0.34)
    }
    .width('100%')
    .height('100%')
  }

  @Builder
  TopBar() {
    Row() {
      Column({ space: 2 }) {
        Text('仿生鱼控制台')
          .fontSize(24)
          .fontWeight(FontWeight.Bold)
          .fontColor(TEXT_MAIN)
        Text('HarmonyOS 上位机 · WebSocket')
          .fontSize(12)
          .fontColor(TEXT_MUTED)
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)

      Button('急停')
        .fontSize(16)
        .fontWeight(FontWeight.Bold)
        .fontColor('#FFFFFF')
        .height(48)
        .width(82)
        .borderRadius(24)
        .backgroundColor(WARNING)
        .shadow({ radius: 20, color: '#66FF6B63', offsetY: 8 })
        .onClick(() => {
          this.applyEmergencyStop();
        })
    }
    .width('100%')
    .alignItems(VerticalAlign.Center)
  }

  @Builder
  HeroConsole() {
    Column({ space: 14 }) {
      Row({ space: 10 }) {
        this.ConnectionPill();
        Text(this.statusMessage)
          .fontSize(12)
          .fontColor(TEXT_MUTED)
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
          .layoutWeight(1)
      }
      .width('100%')

      Row({ space: 10 }) {
        TextInput({ text: this.endpoint, placeholder: 'ws://192.168.4.1/ws' })
          .height(46)
          .fontSize(14)
          .fontColor(TEXT_MAIN)
          .placeholderColor('#72959A')
          .backgroundColor('#2AFFFFFF')
          .borderRadius(16)
          .padding({ left: 14, right: 14 })
          .layoutWeight(1)
          .onChange((value: string) => {
            this.endpoint = value;
          })

        Button(this.connectionState === 'connected' ? '断开' : '连接')
          .height(46)
          .width(76)
          .fontColor('#071317')
          .fontWeight(FontWeight.Bold)
          .borderRadius(16)
          .backgroundColor(CYAN)
          .onClick(() => {
            this.toggleConnection();
          })
      }
      .width('100%')

      Row({ space: 12 }) {
        this.MiniGauge('电池', this.telemetry.battery > 0 ? `${this.telemetry.battery.toFixed(1)}V` : '--');
        this.MiniGauge('深度', `${this.telemetry.depth.toFixed(2)}m`);
        this.MiniGauge('信号', this.telemetry.rssi === 0 ? '--' : `${this.telemetry.rssi}dBm`);
      }
      .width('100%')
    }
    .width('100%')
    .padding(16)
    .backgroundColor(PANEL_BG)
    .backgroundBlurStyle(BlurStyle.COMPONENT_THICK)
    .border({ width: 1, color: PANEL_STROKE })
    .borderRadius(24)
    .shadow({ radius: 30, color: '#30000000', offsetY: 18 })
  }

  @Builder
  ConnectionPill() {
    Row({ space: 8 }) {
      Circle()
        .width(9)
        .height(9)
        .fill(this.stateColor())
        .opacity(this.connectionState === 'connecting' && this.isPulsing ? 0.45 : 1)
      Text(this.stateLabel())
        .fontSize(12)
        .fontWeight(FontWeight.Medium)
        .fontColor(TEXT_MAIN)
    }
    .height(30)
    .padding({ left: 12, right: 12 })
    .borderRadius(15)
    .backgroundColor('#2EFFFFFF')
  }

  @Builder
  MiniGauge(label: string, value: string) {
    Column({ space: 4 }) {
      Text(value)
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .fontColor(TEXT_MAIN)
        .maxLines(1)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
      Text(label)
        .fontSize(11)
        .fontColor(TEXT_MUTED)
    }
    .alignItems(HorizontalAlign.Start)
    .layoutWeight(1)
    .padding({ left: 12, right: 12, top: 10, bottom: 10 })
    .backgroundColor('#22FFFFFF')
    .borderRadius(16)
  }

  @Builder
  ModeTabs() {
    Row({ space: 8 }) {
      this.ModeButton('manual', '手动');
      this.ModeButton('cruise', '巡航');
      this.ModeButton('debug', '调试');
      this.ModeButton('stop', '停止');
    }
    .width('100%')
    .padding(5)
    .backgroundColor('#26FFFFFF')
    .backgroundBlurStyle(BlurStyle.COMPONENT_REGULAR)
    .borderRadius(20)
  }

  @Builder
  ModeButton(mode: FishMode, label: string) {
    Button(label)
      .layoutWeight(1)
      .height(42)
      .fontSize(14)
      .fontWeight(this.command.mode === mode ? FontWeight.Bold : FontWeight.Medium)
      .fontColor(this.command.mode === mode ? '#071317' : TEXT_MAIN)
      .backgroundColor(this.command.mode === mode ? CYAN : '#00FFFFFF')
      .borderRadius(16)
      .onClick(() => {
        this.selectedMode = mode;
        const next = {
          ...this.command,
          mode,
          throttle: mode === 'stop' ? 0 : this.command.throttle,
          yaw: mode === 'stop' ? 0 : this.command.yaw,
          pitch: mode === 'stop' ? 0 : this.command.pitch,
          emergencyStop: false
        };
        this.updateCommand(next);
      })
  }

  @Builder
  ControlPad() {
    Column({ space: 16 }) {
      Row() {
        Column({ space: 4 }) {
          Text('方向控制')
            .fontSize(20)
            .fontWeight(FontWeight.Bold)
            .fontColor(TEXT_MAIN)
          Text('运动意图 · 实时下发')
            .fontSize(12)
            .fontColor(TEXT_MUTED)
        }
        .alignItems(HorizontalAlign.Start)
        .layoutWeight(1)

        Button('回中')
          .height(38)
          .width(72)
          .fontSize(13)
          .fontColor(TEXT_MAIN)
          .backgroundColor('#24FFFFFF')
          .borderRadius(19)
          .onClick(() => {
            this.releaseMotion();
          })
      }
      .width('100%')

      GridRow({ columns: 3, gutter: { x: 12, y: 12 } }) {
        GridCol({ span: 1 }) {
          Blank()
        }
        GridCol({ span: 1 }) {
          this.HoldButton('↑', '上浮', 24, 0, 52)
        }
        GridCol({ span: 1 }) {
          Blank()
        }

        GridCol({ span: 1 }) {
          this.HoldButton('←', '左转', 36, -58, 0)
        }
        GridCol({ span: 1 }) {
          this.HoldButton('▶', '前进', 62, 0, 0)
        }
        GridCol({ span: 1 }) {
          this.HoldButton('→', '右转', 36, 58, 0)
        }

        GridCol({ span: 1 }) {
          this.HoldButton('↺', '微左', 18, -28, 0)
        }
        GridCol({ span: 1 }) {
          this.HoldButton('↓', '下潜', 24, 0, -52)
        }
        GridCol({ span: 1 }) {
          this.HoldButton('↻', '微右', 18, 28, 0)
        }
      }
      .width('100%')

      Column({ space: 12 }) {
        this.ValueSlider('推进', this.command.throttle, 0, 100, '%', (value: number) => {
          this.updateCommand({ ...this.command, mode: this.selectedMode, throttle: Math.round(value), emergencyStop: false });
        });
        this.ValueSlider('转向', this.command.yaw, -100, 100, '', (value: number) => {
          this.updateCommand({ ...this.command, mode: this.selectedMode, yaw: Math.round(value), emergencyStop: false });
        });
        this.ValueSlider('俯仰', this.command.pitch, -100, 100, '', (value: number) => {
          this.updateCommand({ ...this.command, mode: this.selectedMode, pitch: Math.round(value), emergencyStop: false });
        });
      }
      .width('100%')
    }
    .width('100%')
    .padding(16)
    .backgroundColor(PANEL_BG)
    .backgroundBlurStyle(BlurStyle.COMPONENT_THICK)
    .border({ width: 1, color: PANEL_STROKE })
    .borderRadius(24)
  }

  @Builder
  HoldButton(symbol: string, label: string, throttle: number, yaw: number, pitch: number) {
    Column({ space: 2 }) {
      Text(symbol)
        .fontSize(23)
        .fontWeight(FontWeight.Bold)
        .fontColor('#071317')
      Text(label)
        .fontSize(11)
        .fontColor('#16373D')
        .maxLines(1)
    }
    .width('100%')
    .aspectRatio(1)
    .justifyContent(FlexAlign.Center)
    .backgroundColor(CYAN)
    .linearGradient({
      angle: 135,
      colors: [
        ['#38E7CA', 0],
        ['#39A8FF', 1]
      ]
    })
    .borderRadius(22)
    .shadow({ radius: 20, color: '#3538E7CA', offsetY: 10 })
    .scale({
      x: this.command.throttle === throttle && this.command.yaw === yaw && this.command.pitch === pitch ? 0.96 : 1,
      y: this.command.throttle === throttle && this.command.yaw === yaw && this.command.pitch === pitch ? 0.96 : 1
    })
    .animation({ duration: 160, curve: Curve.EaseOut })
    .onTouch((event: TouchEvent) => {
      if (event.type === TouchType.Down) {
        this.updateCommand({
          ...this.command,
          mode: this.selectedMode === 'stop' ? 'manual' : this.selectedMode,
          throttle,
          yaw,
          pitch,
          emergencyStop: false
        });
      }
      if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
        this.releaseMotion();
      }
    })
  }

  @Builder
  ValueSlider(label: string, value: number, min: number, max: number, unit: string, onValue: (value: number) => void) {
    Column({ space: 8 }) {
      Row() {
        Text(label)
          .fontSize(13)
          .fontColor(TEXT_MUTED)
          .layoutWeight(1)
        Text(`${value.toFixed(max <= 5 ? 1 : 0)}${unit}`)
          .fontSize(13)
          .fontWeight(FontWeight.Bold)
          .fontColor(TEXT_MAIN)
      }
      Slider({ value, min, max, step: max <= 5 ? 0.1 : 1 })
        .selectedColor(CYAN)
        .trackColor('#2FFFFFFF')
        .blockColor('#F3FFFC')
        .onChange((next: number) => {
          onValue(next);
        })
    }
    .width('100%')
  }

  @Builder
  TelemetryGrid() {
    Column({ space: 14 }) {
      Row() {
        Text('实时遥测')
          .fontSize(20)
          .fontWeight(FontWeight.Bold)
          .fontColor(TEXT_MAIN)
          .layoutWeight(1)
        Text(this.telemetry.stm32Online ? 'STM32 在线' : 'STM32 离线')
          .fontSize(12)
          .fontColor(this.telemetry.stm32Online ? CYAN : WARNING)
          .padding({ left: 10, right: 10, top: 6, bottom: 6 })
          .backgroundColor(this.telemetry.stm32Online ? '#2038E7CA' : '#26FF6B63')
          .borderRadius(14)
      }
      .width('100%')

      GridRow({ columns: 2, gutter: { x: 12, y: 12 } }) {
        GridCol({ span: 1 }) {
          this.TelemetryCard('横滚', `${this.telemetry.roll.toFixed(1)}°`, this.attitudeColor(this.telemetry.roll))
        }
        GridCol({ span: 1 }) {
          this.TelemetryCard('俯仰', `${this.telemetry.pitch.toFixed(1)}°`, this.attitudeColor(this.telemetry.pitch))
        }
        GridCol({ span: 1 }) {
          this.TelemetryCard('航向', `${this.telemetry.yaw.toFixed(1)}°`, BLUE)
        }
        GridCol({ span: 1 }) {
          this.TelemetryCard('错误码', `${this.telemetry.error}`, this.telemetry.error === 0 ? CYAN : WARNING)
        }
      }
      .width('100%')

      Text(this.ackMessage)
        .fontSize(12)
        .fontColor(TEXT_MUTED)
        .maxLines(1)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
        .width('100%')
    }
    .width('100%')
    .padding(16)
    .backgroundColor(PANEL_BG)
    .backgroundBlurStyle(BlurStyle.COMPONENT_THICK)
    .border({ width: 1, color: PANEL_STROKE })
    .borderRadius(24)
  }

  @Builder
  TelemetryCard(label: string, value: string, color: ResourceColor) {
    Column({ space: 6 }) {
      Text(value)
        .fontSize(22)
        .fontWeight(FontWeight.Bold)
        .fontColor(TEXT_MAIN)
        .maxLines(1)
      Text(label)
        .fontSize(12)
        .fontColor(TEXT_MUTED)
      Rect()
        .width('100%')
        .height(3)
        .fill(color)
        .opacity(0.8)
        .borderRadius(2)
    }
    .alignItems(HorizontalAlign.Start)
    .width('100%')
    .padding(14)
    .backgroundColor('#22FFFFFF')
    .borderRadius(18)
  }

  @Builder
  ParameterDeck() {
    Column({ space: 16 }) {
      Text('动作参数')
        .fontSize(20)
        .fontWeight(FontWeight.Bold)
        .fontColor(TEXT_MAIN)
        .width('100%')

      GridRow({ columns: 3, gutter: { x: 10, y: 10 } }) {
        ForEach(PRESETS, (preset: CommandPreset) => {
          GridCol({ span: 1 }) {
            Button(preset.name)
              .height(42)
              .width('100%')
              .fontSize(13)
              .fontColor(TEXT_MAIN)
              .backgroundColor('#24FFFFFF')
              .borderRadius(16)
              .onClick(() => {
                this.selectedMode = preset.command.mode;
                this.updateCommand({ ...preset.command, emergencyStop: false });
              })
          }
        }, (preset: CommandPreset) => preset.name)
      }
      .width('100%')

      this.ValueSlider('目标深度', this.command.depthTarget, 0, 3, 'm', (value: number) => {
        this.updateCommand({ ...this.command, depthTarget: Number(value.toFixed(2)), emergencyStop: false });
      });
      this.ValueSlider('摆尾幅度', this.command.tailAmp, 0, 100, '%', (value: number) => {
        this.updateCommand({ ...this.command, tailAmp: Math.round(value), emergencyStop: false });
      });
      this.ValueSlider('摆尾频率', this.command.tailFreq, 0, 5, 'Hz', (value: number) => {
        this.updateCommand({ ...this.command, tailFreq: Number(value.toFixed(1)), emergencyStop: false });
      });
      this.ValueSlider('舵机微调', this.command.trim, -20, 20, '', (value: number) => {
        this.updateCommand({ ...this.command, trim: Math.round(value), emergencyStop: false });
      });

      Button('下发限幅与舵机中位')
        .width('100%')
        .height(48)
        .fontSize(15)
        .fontWeight(FontWeight.Bold)
        .fontColor('#071317')
        .backgroundColor(VIOLET)
        .borderRadius(18)
        .onClick(() => {
          this.client?.sendConfig(this.config);
        })
    }
    .width('100%')
    .padding(16)
    .backgroundColor(PANEL_BG)
    .backgroundBlurStyle(BlurStyle.COMPONENT_THICK)
    .border({ width: 1, color: PANEL_STROKE })
    .borderRadius(24)
  }

  @Builder
  RawConsole() {
    Column({ space: 8 }) {
      Text('最近回包')
        .fontSize(14)
        .fontWeight(FontWeight.Bold)
        .fontColor(TEXT_MAIN)
        .width('100%')
      Text(this.rawMessage.length > 0 ? this.rawMessage : '{"type":"telemetry", ...}')
        .fontSize(11)
        .fontColor(TEXT_MUTED)
        .fontFamily('monospace')
        .maxLines(3)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
        .width('100%')
    }
    .width('100%')
    .padding(14)
    .backgroundColor('#1E000000')
    .backgroundBlurStyle(BlurStyle.COMPONENT_REGULAR)
    .border({ width: 1, color: '#26FFFFFF' })
    .borderRadius(18)
  }

  private toggleConnection(): void {
    if (this.connectionState === 'connected' || this.connectionState === 'connecting') {
      this.client?.disconnect();
      return;
    }
    this.client?.connect(this.endpoint.trim().length > 0 ? this.endpoint.trim() : DEFAULT_ENDPOINT);
  }

  private updateCommand(command: FishCommand): void {
    const next: FishCommand = {
      ...command,
      throttle: Math.round(clamp(command.throttle, 0, 100)),
      yaw: Math.round(clamp(command.yaw, -100, 100)),
      pitch: Math.round(clamp(command.pitch, -100, 100)),
      depthTarget: Number(clamp(command.depthTarget, 0, 3).toFixed(2)),
      tailAmp: Math.round(clamp(command.tailAmp, 0, 100)),
      tailFreq: Number(clamp(command.tailFreq, 0, 5).toFixed(1)),
      trim: Math.round(clamp(command.trim, -20, 20))
    };
    this.command = next;
    this.client?.updateCommand(next);
  }

  private releaseMotion(): void {
    this.updateCommand({
      ...this.command,
      mode: this.selectedMode === 'stop' ? 'manual' : this.selectedMode,
      throttle: 0,
      yaw: 0,
      pitch: 0,
      emergencyStop: false
    });
  }

  private applyEmergencyStop(): void {
    this.selectedMode = 'stop';
    const stopped: FishCommand = {
      ...DEFAULT_COMMAND,
      mode: 'stop',
      emergencyStop: true
    };
    this.command = stopped;
    this.client?.emergencyStop();
  }

  private stateLabel(): string {
    switch (this.connectionState) {
      case 'connected':
        return '已连接';
      case 'connecting':
        return '连接中';
      case 'error':
        return '异常';
      case 'closed':
        return '已断开';
      default:
        return '待机';
    }
  }

  private stateColor(): ResourceColor {
    switch (this.connectionState) {
      case 'connected':
        return CYAN;
      case 'connecting':
        return BLUE;
      case 'error':
        return WARNING;
      default:
        return '#8BA7AA';
    }
  }

  private attitudeColor(value: number): ResourceColor {
    return Math.abs(value) > 25 ? WARNING : CYAN;
  }
}
