import { FrontFogIcon, LowBeamIcon, PositionIcon, RearFogIcon } from './icons'

interface LightKnobProps {
  knob: 0 | 1 | 2
  pull: 0 | 1 | 2
  onKnob: (v: 0 | 1 | 2) => void
  onPull: () => void
}

/** 档位图标相对旋钮的角度(顺时针, 0=正上方) */
const DETENTS = [
  { value: 0 as const, angle: -8, label: '0', icon: null },
  { value: 1 as const, angle: 40, label: '示宽灯', icon: PositionIcon },
  { value: 2 as const, angle: 88, label: '近光灯', icon: LowBeamIcon },
]

/** 大众风格旋转式大灯开关(可外拉两档开雾灯) */
export default function LightKnob({ knob, pull, onKnob, onPull }: LightKnobProps) {
  const current = DETENTS[knob]
  const pullable = knob >= 1

  return (
    <div className="flex flex-col items-center select-none">
      <div className="relative h-[240px] w-[240px]">
        {/* 档位图标(面板印刷) */}
        {DETENTS.map((d) => {
          const rad = (d.angle * Math.PI) / 180
          const R = 100
          const x = 120 + Math.sin(rad) * R
          const y = 120 - Math.cos(rad) * R
          const Icon = d.icon
          return (
            <button
              key={d.value}
              onClick={() => onKnob(d.value)}
              className="absolute -translate-x-1/2 -translate-y-1/2 rounded-full p-1.5 transition-colors hover:bg-white/5"
              style={{ left: x, top: y, color: knob === d.value ? '#e8eaee' : '#5f646b' }}
              title={d.label}
            >
              {Icon ? <Icon size={30} /> : <span className="block w-[30px] text-center text-xl font-bold">0</span>}
            </button>
          )
        })}

        {/* 旋钮本体 */}
        <button
          onClick={onPull}
          title={pullable ? '点击外拉/推回: 雾灯' : '先转到示宽灯或近光灯才能拉雾灯'}
          className="absolute left-1/2 top-1/2 h-[128px] w-[128px] -translate-x-1/2 -translate-y-1/2 rounded-full outline-none transition-transform duration-200"
          style={{
            transform: `translate(-50%,-50%) scale(${pull > 0 ? 1.07 : 1}) translateY(${pull > 0 ? -4 : 0}px)`,
            background:
              'radial-gradient(circle at 35% 30%, #3a3d42 0%, #232528 45%, #101114 100%)',
            boxShadow:
              pull > 0
                ? '0 10px 22px rgba(0,0,0,0.85), 0 0 0 3px #2c2f34, inset 0 2px 3px rgba(255,255,255,0.12), 0 0 18px rgba(255,176,32,0.25)'
                : '0 8px 18px rgba(0,0,0,0.8), 0 0 0 3px #2c2f34, inset 0 2px 3px rgba(255,255,255,0.12)',
          }}
        >
          {/* 防滑纹 */}
          {Array.from({ length: 24 }).map((_, i) => (
            <span
              key={i}
              className="absolute left-1/2 top-1/2 h-[58px] w-[5px] rounded-sm bg-black/50"
              style={{ transform: `translate(-50%,-50%) rotate(${i * 15}deg) translateY(-56px)` }}
            />
          ))}
          {/* 档位指针(随档位旋转) */}
          <span
            className="absolute left-1/2 top-1/2 h-[52px] w-[6px] rounded-full bg-[#e8eaee] transition-transform duration-200"
            style={{
              transform: `translate(-50%,-50%) rotate(${current.angle}deg) translateY(-46px)`,
              boxShadow: '0 0 6px rgba(255,255,255,0.35)',
            }}
          />
          {/* 中心 VW 风格凹槽 */}
          <span className="absolute left-1/2 top-1/2 h-[46px] w-[46px] -translate-x-1/2 -translate-y-1/2 rounded-full bg-[#0c0d0f] shadow-[inset_0_2px_6px_rgba(0,0,0,0.9),inset_0_-1px_2px_rgba(255,255,255,0.08)]" />
        </button>
      </div>

      {/* 外拉状态指示 */}
      <div className="mt-1 flex items-center gap-4 text-xs">
        <span
          className="flex items-center gap-1.5"
          style={{ color: pull >= 1 ? '#3ddc84' : '#565b63', textShadow: pull >= 1 ? '0 0 8px #3ddc84aa' : 'none' }}
        >
          <FrontFogIcon size={18} /> 一档·前雾灯
        </span>
        <span
          className="flex items-center gap-1.5"
          style={{ color: pull === 2 ? '#ffb020' : '#565b63', textShadow: pull === 2 ? '0 0 8px #ffb020aa' : 'none' }}
        >
          <RearFogIcon size={18} /> 二档·后雾灯
        </span>
      </div>
      <p className="mt-1 text-[11px] text-[#565b63]">点图标旋转 · 点旋钮外拉</p>
    </div>
  )
}
