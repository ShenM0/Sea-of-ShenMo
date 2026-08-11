import type { LightState } from '../exam/bank'
import {
  FrontFogIcon,
  HazardIcon,
  HighBeamIcon,
  LowBeamIcon,
  PositionIcon,
  RearFogIcon,
  TurnArrowIcon,
} from './icons'

interface ClusterProps {
  lights: LightState
  flashActive: boolean
  lcdTitle: string
  lcdSub: string
  lcdTone: 'idle' | 'run' | 'pass' | 'fail'
}

/** 单个大众风格圆形仪表 */
function Gauge({ label, unit, max, ticks }: { label: string; unit: string; max: number; ticks: number[] }) {
  const cx = 100
  const cy = 100
  const r = 78
  // 0 在左下(7:30 方向), 顺时针扫过左侧-顶部-右侧到 4:30, 与真实车表一致
  const startA = 225
  const sweep = 270
  const tickEls = []
  for (let i = 0; i < ticks.length; i++) {
    const a = ((startA - (sweep * i) / (ticks.length - 1)) * Math.PI) / 180
    const x1 = cx + Math.cos(a) * r
    const y1 = cy - Math.sin(a) * r
    const x2 = cx + Math.cos(a) * (r - 10)
    const y2 = cy - Math.sin(a) * (r - 10)
    const xt = cx + Math.cos(a) * (r - 24)
    const yt = cy - Math.sin(a) * (r - 24)
    const red = unit === 'x1000' && ticks[i] >= 6
    tickEls.push(
      <g key={i}>
        <line x1={x1} y1={y1} x2={x2} y2={y2} stroke={red ? '#e3352b' : '#d8dbe2'} strokeWidth="3" />
        <text
          x={xt}
          y={yt}
          fill={red ? '#e3352b' : '#d8dbe2'}
          fontSize="15"
          fontWeight="600"
          textAnchor="middle"
          dominantBaseline="central"
          fontFamily="Arial, sans-serif"
        >
          {ticks[i]}
        </text>
      </g>,
    )
  }
  // 指针停在 0 位
  const a0 = (startA * Math.PI) / 180
  const nx = cx + Math.cos(a0) * (r - 16)
  const ny = cy - Math.sin(a0) * (r - 16)
  return (
    <svg viewBox="0 0 200 200" className="w-full max-w-[210px]">
      <circle cx={cx} cy={cy} r="97" fill="#0b0c0e" stroke="url(#chrome)" strokeWidth="5" />
      <circle cx={cx} cy={cy} r="90" fill="none" stroke="#1c1e22" strokeWidth="1" />
      <defs>
        <linearGradient id="chrome" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0%" stopColor="#9aa0a8" />
          <stop offset="45%" stopColor="#e8eaee" />
          <stop offset="100%" stopColor="#5c6167" />
        </linearGradient>
      </defs>
      {tickEls}
      <text x={cx} y={cy + 46} fill="#8b9098" fontSize="13" textAnchor="middle" fontFamily="Arial, sans-serif">
        {label}
      </text>
      <text x={cx} y={cy + 64} fill="#5f646b" fontSize="10" textAnchor="middle" fontFamily="Arial, sans-serif">
        {unit}
      </text>
      <line x1={cx} y1={cy} x2={nx} y2={ny} stroke="#e3352b" strokeWidth="4" strokeLinecap="round" />
      <circle cx={cx} cy={cy} r="8" fill="#141518" stroke="#3a3d42" strokeWidth="2" />
      <text x={cx} y={cy + r + 14} fill="transparent" fontSize="1">
        {max}
      </text>
    </svg>
  )
}

function Indicator({
  on,
  blink,
  color,
  label,
  children,
}: {
  on: boolean
  blink?: boolean
  color: string
  label: string
  children: React.ReactNode
}) {
  return (
    <div
      className={`flex flex-col items-center gap-1 transition-opacity duration-150 ${on ? '' : 'opacity-[0.16]'}`}
      title={label}
    >
      <div
        className={blink && on ? 'indicator-blink' : ''}
        style={{
          color,
          filter: on ? `drop-shadow(0 0 7px ${color}) drop-shadow(0 0 2px ${color})` : 'none',
        }}
      >
        {children}
      </div>
      <span className="text-[10px] leading-none tracking-wide" style={{ color: on ? color : '#565b63' }}>
        {label}
      </span>
    </div>
  )
}

/** 大众风格仪表盘总成 */
export default function Cluster({ lights, flashActive, lcdTitle, lcdSub, lcdTone }: ClusterProps) {
  const lowOn = lights.knob === 2 && !lights.highBeam && !flashActive
  const highOn = (lights.knob === 2 && lights.highBeam) || flashActive
  const posOn = lights.knob >= 1
  const frontFogOn = lights.knob >= 1 && lights.pull >= 1
  const rearFogOn = lights.knob >= 1 && lights.pull === 2
  const leftOn = lights.turn === 'left' || lights.hazard
  const rightOn = lights.turn === 'right' || lights.hazard

  const lcdColor =
    lcdTone === 'fail' ? '#ff5a4e' : lcdTone === 'pass' ? '#3ddc84' : lcdTone === 'run' ? '#dfe6f3' : '#9aa3b2'

  return (
    <div className="relative rounded-[28px] border border-[#26282d] bg-gradient-to-b from-[#17181b] via-[#101114] to-[#0a0b0d] px-5 py-4 shadow-[inset_0_1px_0_rgba(255,255,255,0.06),0_18px_40px_rgba(0,0,0,0.55)]">
      {/* 顶部转向箭头 */}
      <div className="pointer-events-none absolute left-6 top-4">
        <div
          className={leftOn ? 'indicator-blink' : 'opacity-[0.14]'}
          style={{ color: '#2ecc71', filter: leftOn ? 'drop-shadow(0 0 8px #2ecc71)' : 'none' }}
        >
          <TurnArrowIcon dir="left" size={34} />
        </div>
      </div>
      <div className="pointer-events-none absolute right-6 top-4">
        <div
          className={rightOn ? 'indicator-blink' : 'opacity-[0.14]'}
          style={{ color: '#2ecc71', filter: rightOn ? 'drop-shadow(0 0 8px #2ecc71)' : 'none' }}
        >
          <TurnArrowIcon dir="right" size={34} />
        </div>
      </div>

      <div className="flex items-start justify-center gap-2 sm:gap-6">
        <Gauge label="发动机转速" unit="x1000 r/min" max={8} ticks={[0, 1, 2, 3, 4, 5, 6, 7, 8]} />
        <Gauge label="车速" unit="km/h" max={260} ticks={[0, 40, 80, 120, 160, 200, 240]} />
      </div>

      {/* 中央液晶屏 */}
      <div className="mx-auto -mt-2 w-full max-w-md rounded-md border border-[#2a2e36] bg-[#0d1420] px-4 py-3 text-center shadow-[inset_0_2px_10px_rgba(0,0,0,0.8)]">
        <div
          className="min-h-[26px] text-[15px] font-semibold tracking-wide transition-colors sm:text-base"
          style={{ color: lcdColor, textShadow: `0 0 8px ${lcdColor}55` }}
        >
          {lcdTitle}
        </div>
        <div className="mt-1 min-h-[16px] text-xs text-[#6f7a8d]">{lcdSub}</div>
      </div>

      {/* 指示灯排 */}
      <div className="mt-4 flex items-end justify-center gap-4 sm:gap-7">
        <Indicator on={posOn} color="#3ddc84" label="示宽灯">
          <PositionIcon size={30} />
        </Indicator>
        <Indicator on={lowOn} color="#3ddc84" label="近光灯">
          <LowBeamIcon size={30} />
        </Indicator>
        <Indicator on={highOn} color="#3f8cff" label="远光灯">
          <HighBeamIcon size={30} />
        </Indicator>
        <Indicator on={frontFogOn} color="#3ddc84" label="前雾灯">
          <FrontFogIcon size={30} />
        </Indicator>
        <Indicator on={rearFogOn} color="#ffb020" label="后雾灯">
          <RearFogIcon size={30} />
        </Indicator>
        <Indicator on={lights.hazard} blink color="#ff5a4e" label="双闪">
          <HazardIcon size={26} />
        </Indicator>
      </div>
    </div>
  )
}
