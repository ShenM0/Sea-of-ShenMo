// 标准汽车灯光指示灯图标 (ISO 2575)

interface IconProps {
  size?: number
  className?: string
}

/** 近光灯: 灯碗 + 向左下斜的光线 */
export function LowBeamIcon({ size = 32, className }: IconProps) {
  return (
    <svg width={size} height={size} viewBox="0 0 48 48" className={className} fill="none">
      <path d="M30 10 C38 15 38 33 30 38 L30 10 Z" stroke="currentColor" strokeWidth="3" fill="none" strokeLinejoin="round" />
      <g stroke="currentColor" strokeWidth="3" strokeLinecap="round">
        <line x1="22" y1="12" x2="8" y2="18" />
        <line x1="22" y1="20" x2="8" y2="26" />
        <line x1="22" y1="28" x2="8" y2="34" />
        <line x1="22" y1="36" x2="10" y2="41" />
      </g>
    </svg>
  )
}

/** 远光灯: 灯碗 + 水平直射光线 */
export function HighBeamIcon({ size = 32, className }: IconProps) {
  return (
    <svg width={size} height={size} viewBox="0 0 48 48" className={className} fill="none">
      <path d="M30 10 C38 15 38 33 30 38 L30 10 Z" stroke="currentColor" strokeWidth="3" fill="none" strokeLinejoin="round" />
      <g stroke="currentColor" strokeWidth="3" strokeLinecap="round">
        <line x1="24" y1="13" x2="8" y2="13" />
        <line x1="24" y1="20" x2="8" y2="20" />
        <line x1="24" y1="28" x2="8" y2="28" />
        <line x1="24" y1="35" x2="8" y2="35" />
      </g>
    </svg>
  )
}

/** 示宽灯: 左右两个灯碗 */
export function PositionIcon({ size = 32, className }: IconProps) {
  return (
    <svg width={size} height={size} viewBox="0 0 48 48" className={className} fill="none">
      <path d="M18 12 C12 16 12 32 18 36 L18 12 Z" stroke="currentColor" strokeWidth="3" strokeLinejoin="round" />
      <path d="M30 12 C36 16 36 32 30 36 L30 12 Z" stroke="currentColor" strokeWidth="3" strokeLinejoin="round" />
      <g stroke="currentColor" strokeWidth="3" strokeLinecap="round">
        <line x1="12" y1="18" x2="4" y2="18" />
        <line x1="12" y1="24" x2="4" y2="24" />
        <line x1="12" y1="30" x2="4" y2="30" />
        <line x1="36" y1="18" x2="44" y2="18" />
        <line x1="36" y1="24" x2="44" y2="24" />
        <line x1="36" y1="30" x2="44" y2="30" />
      </g>
    </svg>
  )
}

/** 前雾灯: 光线 + 波浪竖线(光在前) */
export function FrontFogIcon({ size = 32, className }: IconProps) {
  return (
    <svg width={size} height={size} viewBox="0 0 48 48" className={className} fill="none">
      <path d="M34 10 C42 15 42 33 34 38 L34 10 Z" stroke="currentColor" strokeWidth="3" strokeLinejoin="round" />
      <path d="M18 8 C14 14 22 18 18 24 C14 30 22 34 18 40" stroke="currentColor" strokeWidth="3" strokeLinecap="round" />
      <g stroke="currentColor" strokeWidth="3" strokeLinecap="round">
        <line x1="28" y1="14" x2="8" y2="20" />
        <line x1="28" y1="23" x2="8" y2="29" />
        <line x1="28" y1="32" x2="10" y2="37" />
      </g>
    </svg>
  )
}

/** 后雾灯: 灯碗在左,光线向右被波浪线遮挡 */
export function RearFogIcon({ size = 32, className }: IconProps) {
  return (
    <svg width={size} height={size} viewBox="0 0 48 48" className={className} fill="none">
      <path d="M14 10 C6 15 6 33 14 38 L14 10 Z" stroke="currentColor" strokeWidth="3" strokeLinejoin="round" />
      <path d="M30 8 C26 14 34 18 30 24 C26 30 34 34 30 40" stroke="currentColor" strokeWidth="3" strokeLinecap="round" />
      <g stroke="currentColor" strokeWidth="3" strokeLinecap="round">
        <line x1="20" y1="14" x2="38" y2="14" />
        <line x1="20" y1="23" x2="40" y2="23" />
        <line x1="20" y1="32" x2="38" y2="32" />
      </g>
    </svg>
  )
}

/** 转向灯箭头 */
export function TurnArrowIcon({ size = 32, dir, className }: IconProps & { dir: 'left' | 'right' }) {
  return (
    <svg width={size} height={size} viewBox="0 0 48 48" className={className} fill="none">
      <path
        d={dir === 'left' ? 'M20 8 L6 24 L20 40 M6 24 L42 24' : 'M28 8 L42 24 L28 40 M6 24 L42 24'}
        stroke="currentColor"
        strokeWidth="4"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  )
}

/** 危险报警三角 */
export function HazardIcon({ size = 24, className }: IconProps) {
  return (
    <svg width={size} height={size} viewBox="0 0 48 48" className={className} fill="none">
      <path d="M24 8 L44 40 L4 40 Z" stroke="currentColor" strokeWidth="4" strokeLinejoin="round" />
      <path d="M24 20 L35 36 L13 36 Z" stroke="currentColor" strokeWidth="3" strokeLinejoin="round" />
    </svg>
  )
}
