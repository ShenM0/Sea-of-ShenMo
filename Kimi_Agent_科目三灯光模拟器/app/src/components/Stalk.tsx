interface StalkProps {
  turn: 'left' | 'right' | null
  highBeam: boolean
  flashActive: boolean
  onTurn: (dir: 'left' | 'right') => void
  onFlashDown: () => void
  onFlashUp: () => void
  onHighBeam: () => void
}

/** 大众风格组合拨杆(转向 / 远光 / 闪灯) */
export default function Stalk({
  turn,
  highBeam,
  flashActive,
  onTurn,
  onFlashDown,
  onFlashUp,
  onHighBeam,
}: StalkProps) {
  const tilt = turn === 'right' ? -5 : turn === 'left' ? 5 : 0

  return (
    <div className="relative flex flex-col items-center select-none">
      {/* 方向盘暗示(背景) */}
      <div
        className="pointer-events-none absolute -bottom-48 right-[-70px] h-[300px] w-[300px] rounded-full opacity-45"
        style={{
          background:
            'radial-gradient(circle, transparent 52%, #1b1d20 53%, #26282c 66%, #111214 68%, transparent 70%)',
        }}
      />

      <div className="relative flex w-full max-w-[420px] items-center">
        {/* 上下抬压按钮 */}
        <div className="absolute -top-1 left-16 flex -translate-y-full flex-col items-center">
          <button
            onClick={() => onTurn('right')}
            className={`rounded-lg border px-3 py-1 text-xs transition-all ${
              turn === 'right'
                ? 'border-[#3ddc84] bg-[#3ddc84]/15 text-[#3ddc84]'
                : 'border-[#33363c] bg-[#1b1d20] text-[#9aa0a8] hover:border-[#4a4e55]'
            }`}
          >
            ▲ 上抬 · 右转向
          </button>
        </div>
        <div className="absolute -bottom-1 left-16 flex translate-y-full flex-col items-center">
          <button
            onClick={() => onTurn('left')}
            className={`rounded-lg border px-3 py-1 text-xs transition-all ${
              turn === 'left'
                ? 'border-[#3ddc84] bg-[#3ddc84]/15 text-[#3ddc84]'
                : 'border-[#33363c] bg-[#1b1d20] text-[#9aa0a8] hover:border-[#4a4e55]'
            }`}
          >
            ▼ 下压 · 左转向
          </button>
        </div>

        {/* 拨杆本体 */}
        <div
          className="relative z-10 flex h-[54px] w-[250px] items-center rounded-l-[26px] transition-transform duration-150"
          style={{
            transform: `rotate(${tilt}deg) translateX(${flashActive ? 8 : 0}px)`,
            transformOrigin: 'right center',
            background: 'linear-gradient(180deg, #2e3136 0%, #1a1c1f 55%, #0d0e10 100%)',
            boxShadow:
              '0 6px 14px rgba(0,0,0,0.7), inset 0 1px 1px rgba(255,255,255,0.12), inset 0 -2px 4px rgba(0,0,0,0.6)',
          }}
        >
          {/* 手柄防滑纹 */}
          <div className="ml-4 flex gap-[5px]">
            {Array.from({ length: 7 }).map((_, i) => (
              <span key={i} className="h-[34px] w-[4px] rounded-sm bg-black/60 shadow-[inset_0_1px_1px_rgba(255,255,255,0.08)]" />
            ))}
          </div>
          {/* 杆面印刷图标 */}
          <div className="ml-5 flex items-center gap-2 text-[#c9cdd4]">
            <svg width="16" height="20" viewBox="0 0 16 20">
              <path d="M8 1 L13 6 L3 6 Z M8 19 L3 14 L13 14 Z" fill="currentColor" opacity="0.85" />
            </svg>
            <span className="text-[9px] tracking-widest text-[#7d838c]">灯光拨杆</span>
          </div>
          {/* 拉(闪灯)区: 靠近驾驶员一侧 */}
          <button
            onPointerDown={onFlashDown}
            onPointerUp={onFlashUp}
            onPointerLeave={onFlashUp}
            onContextMenu={(e) => e.preventDefault()}
            className={`absolute inset-y-1 left-1 w-[86px] rounded-l-[22px] border transition-colors touch-none ${
              flashActive ? 'border-[#3f8cff]/70 bg-[#3f8cff]/20' : 'border-white/10 bg-white/[0.03] hover:bg-white/[0.07]'
            }`}
            title="向怀里拉: 远近光交替(按住)"
          >
            <span className="absolute -bottom-5 left-0 w-full whitespace-nowrap text-center text-[10px] text-[#7d838c]">
              按住 · 远近交替
            </span>
          </button>
        </div>

        {/* 转向柱护罩 */}
        <div
          className="relative z-0 -ml-2 h-[120px] w-[110px] rounded-r-[40px]"
          style={{
            background: 'linear-gradient(180deg, #26282d 0%, #151619 60%, #0c0d0f 100%)',
            boxShadow: 'inset 0 1px 1px rgba(255,255,255,0.08), 6px 8px 18px rgba(0,0,0,0.6)',
          }}
        >
          {/* 推(远光)按钮嵌在护罩上 */}
          <button
            onClick={onHighBeam}
            className={`absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2 whitespace-nowrap rounded-xl border px-3 py-2 text-xs font-medium transition-all ${
              highBeam
                ? 'border-[#3f8cff] bg-[#3f8cff]/20 text-[#6fa8ff] shadow-[0_0_14px_rgba(63,140,255,0.35)]'
                : 'border-[#3a3d43] bg-[#1d1f23] text-[#9aa0a8] hover:border-[#4a4e55]'
            }`}
          >
            向外推·远光灯{highBeam ? '(开)' : ''}
          </button>
        </div>
      </div>
    </div>
  )
}
