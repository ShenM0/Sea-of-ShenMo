// 科目三模拟夜间灯光考试 —— 题库与判定逻辑

/** 车辆灯光状态 */
export interface LightState {
  /** 大灯旋钮: 0=关闭 1=示宽灯 2=前照灯 */
  knob: 0 | 1 | 2
  /** 旋钮外拉: 0=未拉 1=前雾灯 2=后雾灯 */
  pull: 0 | 1 | 2
  /** 拨杆外推: 远光灯 */
  highBeam: boolean
  /** 危险报警闪光灯(双闪) */
  hazard: boolean
  /** 转向灯 */
  turn: 'left' | 'right' | null
}

export const initialLights: LightState = {
  knob: 0,
  pull: 0,
  highBeam: false,
  hazard: false,
  turn: null,
}

export type AnswerType = 'low' | 'high' | 'flash' | 'park' | 'fog' | 'off'

export interface Question {
  id: string
  /** 语音播报文本 */
  prompt: string
  answer: AnswerType
  /** 文字版标准操作 */
  tip: string
}

/** 标准题库(各地通用版) */
export const BANK: Question[] = [
  // 近光灯
  { id: 'low1', prompt: '夜间同方向近距离跟车行驶', answer: 'low', tip: '近光灯' },
  { id: 'low2', prompt: '夜间与机动车会车', answer: 'low', tip: '近光灯' },
  { id: 'low3', prompt: '夜间在窄路与非机动车会车', answer: 'low', tip: '近光灯' },
  { id: 'low4', prompt: '夜间在有路灯、照明良好的道路上行驶', answer: 'low', tip: '近光灯' },
  { id: 'low5', prompt: '夜间直行通过路口', answer: 'low', tip: '近光灯' },
  // 远光灯
  { id: 'high1', prompt: '夜间在没有路灯、照明不良的道路上行驶', answer: 'high', tip: '远光灯(拨杆向外推)' },
  // 远近光交替
  { id: 'flash1', prompt: '夜间通过急弯', answer: 'flash', tip: '交替使用远近光灯(拨杆向怀里拉两下)' },
  { id: 'flash2', prompt: '夜间通过坡路', answer: 'flash', tip: '交替使用远近光灯(拨杆向怀里拉两下)' },
  { id: 'flash3', prompt: '夜间通过拱桥', answer: 'flash', tip: '交替使用远近光灯(拨杆向怀里拉两下)' },
  { id: 'flash4', prompt: '夜间通过人行横道', answer: 'flash', tip: '交替使用远近光灯(拨杆向怀里拉两下)' },
  { id: 'flash5', prompt: '夜间通过没有交通信号灯控制的路口', answer: 'flash', tip: '交替使用远近光灯(拨杆向怀里拉两下)' },
  { id: 'flash6', prompt: '夜间超越前方车辆', answer: 'flash', tip: '交替使用远近光灯(拨杆向怀里拉两下)' },
  // 临时停车 / 故障
  { id: 'park1', prompt: '路边临时停车', answer: 'park', tip: '示宽灯 + 危险报警闪光灯' },
  { id: 'park2', prompt: '夜间在道路上发生故障,妨碍交通又难以移动', answer: 'park', tip: '示宽灯 + 危险报警闪光灯' },
  // 雾天
  { id: 'fog1', prompt: '雾天行驶', answer: 'fog', tip: '雾灯 + 危险报警闪光灯' },
]

/** 判定: s 为当前灯光状态, flashed 表示作答窗口内是否做过远近光交替 */
export function checkAnswer(answer: AnswerType, s: LightState, flashed: boolean): boolean {
  switch (answer) {
    case 'low':
      return s.knob === 2 && !s.highBeam && s.pull === 0 && !s.hazard
    case 'high':
      return s.knob === 2 && s.highBeam && !s.hazard
    case 'flash':
      return flashed && s.knob === 2 && !s.highBeam && s.pull === 0 && !s.hazard
    case 'park':
      return s.knob === 1 && s.hazard && !s.highBeam
    case 'fog':
      return s.knob >= 1 && s.pull >= 1 && s.hazard && !s.highBeam
    case 'off':
      // 旋钮归 0 后所有灯光实际熄灭,拨杆锁止位置不影响
      return s.knob === 0 && s.pull === 0 && !s.hazard
  }
}

export const START_PROMPT = '请开启前照灯'
export const END_PROMPT = '模拟夜间考试完成,请关闭所有灯光'
export const INTRO_PROMPT =
  '下面将进行模拟夜间行驶场景灯光使用的考试,请按语音指令,在五秒内做出相应的灯光操作'

function shuffle<T>(arr: T[]): T[] {
  const a = [...arr]
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1))
    ;[a[i], a[j]] = [a[j], a[i]]
  }
  return a
}

/** 抽题: 保证至少一道远近光交替、一道停车/雾天, 类型尽量不重复 */
export function pickQuestions(n: number): Question[] {
  const flashes = shuffle(BANK.filter((q) => q.answer === 'flash'))
  const specials = shuffle(BANK.filter((q) => q.answer === 'park' || q.answer === 'fog'))
  const normals = shuffle(BANK.filter((q) => q.answer === 'low' || q.answer === 'high'))

  const picked: Question[] = []
  if (n >= 1) picked.push(flashes[0])
  if (n >= 2) picked.push(specials[0])
  if (n >= 3) picked.push(flashes[1])
  const restPool = shuffle([...normals, ...flashes.slice(2), ...specials.slice(1)])
  for (const q of restPool) {
    if (picked.length >= n) break
    picked.push(q)
  }
  return shuffle(picked.slice(0, n))
}
