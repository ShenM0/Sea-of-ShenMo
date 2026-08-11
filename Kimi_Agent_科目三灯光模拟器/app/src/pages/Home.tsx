import { useCallback, useEffect, useRef, useState } from 'react'
import {
  BANK,
  END_PROMPT,
  INTRO_PROMPT,
  START_PROMPT,
  checkAnswer,
  initialLights,
  pickQuestions,
  type AnswerType,
  type LightState,
} from '../exam/bank'
import { useBlinkerSound, useSpeech } from '../hooks/useSpeech'
import Cluster from '../components/Cluster'
import LightKnob from '../components/LightKnob'
import Stalk from '../components/Stalk'
import { HazardIcon } from '../components/icons'

interface Step {
  prompt: string
  answer: AnswerType | 'intro'
  tip: string
}

type Mode = 'exam' | 'practice'
type Phase = 'idle' | 'speaking' | 'window' | 'feedback' | 'done'

const WINDOW_MS = 5000

function describeLights(s: LightState): string {
  const parts: string[] = []
  if (s.knob === 2 && s.highBeam) parts.push('远光灯')
  else if (s.knob === 2) parts.push('近光灯')
  else if (s.knob === 1) parts.push('示宽灯')
  if (s.knob >= 1 && s.pull === 1) parts.push('前雾灯')
  if (s.knob >= 1 && s.pull === 2) parts.push('前/后雾灯')
  if (s.hazard) parts.push('双闪')
  if (s.turn) parts.push(s.turn === 'left' ? '左转向灯' : '右转向灯')
  return parts.length ? parts.join(' + ') : '全部关闭'
}

export default function Home() {
  const [lights, setLights] = useState<LightState>(initialLights)
  const [flashActive, setFlashActive] = useState(false)
  const [flashTick, setFlashTick] = useState(0)
  const [mode, setMode] = useState<Mode>('exam')
  const [running, setRunning] = useState(false)
  const [phase, setPhase] = useState<Phase>('idle')
  const [stepIdx, setStepIdx] = useState(0)
  const [countdown, setCountdown] = useState(0)
  const [questionCount, setQuestionCount] = useState(5)
  const [stats, setStats] = useState({ ok: 0, bad: 0 })
  const [result, setResult] = useState<{ pass: boolean; title: string; detail: string } | null>(null)
  const [showBank, setShowBank] = useState(false)
  const [lastGood, setLastGood] = useState(false)

  const { speak, stop, enabled, setEnabled, rate, setRate, supported } = useSpeech()
  useBlinkerSound(lights.turn !== null || lights.hazard)

  const stepsRef = useRef<Step[]>([])
  const lightsRef = useRef(lights)
  const phaseRef = useRef<Phase>('idle')
  const stepIdxRef = useRef(0)
  const flashedRef = useRef(false)
  const timerRef = useRef(0)
  const modeRef = useRef<Mode>('exam')
  lightsRef.current = lights
  phaseRef.current = phase
  stepIdxRef.current = stepIdx
  modeRef.current = mode

  const clearTimer = () => window.clearInterval(timerRef.current)

  const advance = useCallback(() => setStepIdx((i) => i + 1), [])

  const startWindow = useCallback(() => {
    setPhase('window')
    const deadline = Date.now() + WINDOW_MS
    clearTimer()
    timerRef.current = window.setInterval(() => {
      const remain = deadline - Date.now()
      setCountdown(Math.max(0, remain))
      if (remain <= 0) {
        clearTimer()
        handleTimeout()
      }
    }, 50)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const handlePass = useCallback(() => {
    clearTimer()
    setCountdown(0)
    setLastGood(true)
    window.setTimeout(() => setLastGood(false), 900)
    setStats((s) => ({ ...s, ok: s.ok + 1 }))
    setPhase('feedback')
    window.setTimeout(advance, 750)
  }, [advance])

  const handleTimeout = useCallback(() => {
    const step = stepsRef.current[stepIdxRef.current]
    if (!step) return
    if (modeRef.current === 'practice') {
      setStats((s) => ({ ...s, bad: s.bad + 1 }))
      setPhase('feedback')
      speak(`正确操作是,${step.tip}`, () => window.setTimeout(advance, 300))
    } else {
      finish(false, step)
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [speak, advance])

  const finish = useCallback(
    (pass: boolean, failedStep?: Step) => {
      clearTimer()
      setRunning(false)
      setPhase('done')
      setCountdown(0)
      if (pass) {
        setResult({ pass: true, title: '考试结束 · 成绩合格', detail: '全部灯光操作正确,100 分' })
        speak('考试结束,成绩合格')
      } else {
        setResult({
          pass: false,
          title: '考试结束 · 成绩不合格',
          detail: failedStep ? `「${failedStep.prompt}」 正确操作:${failedStep.tip}` : '',
        })
        speak(`操作错误${failedStep ? `,正确操作是,${failedStep.tip}` : ''},考试成绩不合格`)
      }
    },
    [speak],
  )

  // 考试引擎: 逐题播报 → 5 秒作答窗口
  useEffect(() => {
    if (!running) return
    const step = stepsRef.current[stepIdx]
    if (!step) {
      finish(true)
      return
    }
    flashedRef.current = false
    setPhase('speaking')
    setCountdown(0)
    const cancel = speak(step.prompt, () => {
      if (step.answer === 'intro') {
        window.setTimeout(advance, 400)
      } else {
        startWindow()
      }
    })
    return () => {
      cancel?.()
      clearTimer()
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [running, stepIdx])

  // 实时判定灯光状态
  useEffect(() => {
    if (!running || phase !== 'window') return
    const step = stepsRef.current[stepIdx]
    if (!step || step.answer === 'intro') return
    if (checkAnswer(step.answer, lights, flashedRef.current)) handlePass()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [lights, flashTick, phase, running, stepIdx])

  useEffect(
    () => () => {
      clearTimer()
      stop()
    },
    [stop],
  )

  const resetAndStart = (m: Mode) => {
    stop()
    clearTimer()
    setLights(initialLights)
    setFlashActive(false)
    setStats({ ok: 0, bad: 0 })
    setResult(null)
    setMode(m)
    modeRef.current = m
    if (m === 'exam') {
      stepsRef.current = [
        { prompt: INTRO_PROMPT, answer: 'intro', tip: '' },
        { prompt: START_PROMPT, answer: 'low', tip: '近光灯' },
        ...pickQuestions(questionCount),
        { prompt: END_PROMPT, answer: 'off', tip: '关闭所有灯光' },
      ]
    } else {
      const pool = [...pickQuestions(8), ...pickQuestions(8), ...pickQuestions(8)]
      stepsRef.current = pool
    }
    setStepIdx(0)
    setRunning(true)
  }

  const stopAll = () => {
    stop()
    clearTimer()
    setRunning(false)
    setPhase('idle')
    setCountdown(0)
  }

  // ---- 灯光操作 ----
  const onKnob = (v: 0 | 1 | 2) =>
    setLights((s) => ({ ...s, knob: v, pull: v === 0 ? 0 : s.pull }))

  const onPull = () =>
    setLights((s) => (s.knob === 0 ? s : { ...s, pull: ((s.pull + 1) % 3) as 0 | 1 | 2 }))

  const onTurn = (dir: 'left' | 'right') =>
    setLights((s) => ({ ...s, turn: s.turn === dir ? null : dir }))

  const onFlashDown = () => {
    setFlashActive(true)
    flashedRef.current = true
    setFlashTick((t) => t + 1)
  }
  const onFlashUp = () => setFlashActive(false)

  const onHighBeam = () => setLights((s) => ({ ...s, highBeam: !s.highBeam }))
  const onHazard = () => setLights((s) => ({ ...s, hazard: !s.hazard }))

  // ---- 显示 ----
  const currentStep = running ? stepsRef.current[stepIdx] : undefined
  const totalSteps = mode === 'exam' ? stepsRef.current.length - 1 : stepsRef.current.length
  const progressText =
    running && currentStep && currentStep.answer !== 'intro'
      ? mode === 'exam'
        ? `第 ${Math.min(stepIdx, totalSteps)} / ${totalSteps} 项`
        : `练习 · 第 ${stepIdx + 1} 题`
      : ''

  let lcdTitle = '准备就绪'
  let lcdSub = '点击「开始考试」或「开始练习」'
  let lcdTone: 'idle' | 'run' | 'pass' | 'fail' = 'idle'
  if (running) {
    lcdTone = 'run'
    if (phase === 'speaking') {
      lcdTitle = currentStep?.prompt ?? ''
      lcdSub = '语音播报中,请听指令…'
    } else if (phase === 'window') {
      lcdTitle = currentStep?.prompt ?? ''
      lcdSub = '请操作灯光'
    } else if (phase === 'feedback') {
      lcdTitle = lastGood ? '✓ 操作正确' : currentStep?.prompt ?? ''
      lcdSub = lastGood ? '' : '听正确答案…'
      lcdTone = lastGood ? 'pass' : 'run'
    }
  } else if (result) {
    lcdTitle = result.title
    lcdSub = result.detail
    lcdTone = result.pass ? 'pass' : 'fail'
  }

  const seconds = (countdown / 1000).toFixed(1)
  const ringPct = countdown / WINDOW_MS

  return (
    <div className="min-h-screen bg-[#0a0a0b] text-[#e8eaee]">
      {/* 顶部 */}
      <header className="border-b border-[#1e2024] bg-[#0d0e10]">
        <div className="mx-auto flex max-w-6xl flex-wrap items-center gap-x-6 gap-y-2 px-4 py-4">
          <div>
            <h1 className="text-lg font-bold tracking-wide sm:text-xl">
              科目三 · 模拟夜间灯光考试
            </h1>
            <p className="text-xs text-[#7d838c]">大众杆式灯光系统仿真 · 语音播报 · 5 秒限时作答</p>
          </div>
          <div className="ml-auto flex items-center gap-3 text-xs text-[#9aa0a8]">
            <label className="flex items-center gap-2">
              <input
                type="checkbox"
                checked={enabled}
                onChange={(e) => setEnabled(e.target.checked)}
                className="accent-[#3ddc84]"
              />
              语音播报{supported ? '' : '(浏览器不支持)'}
            </label>
            <label className="flex items-center gap-2">
              语速
              <input
                type="range"
                min={0.8}
                max={1.15}
                step={0.05}
                value={rate}
                onChange={(e) => setRate(Number(e.target.value))}
                className="w-20 accent-[#3ddc84]"
              />
            </label>
          </div>
        </div>
      </header>

      <main className="mx-auto max-w-6xl px-4 py-5">
        {/* 控制条 */}
        <div className="mb-5 flex flex-wrap items-center gap-3">
          <button
            onClick={() => resetAndStart('exam')}
            disabled={running}
            className="rounded-lg bg-[#3ddc84] px-5 py-2.5 text-sm font-bold text-black transition-all hover:brightness-110 disabled:cursor-not-allowed disabled:opacity-30"
          >
            ▶ 开始考试
          </button>
          <button
            onClick={() => resetAndStart('practice')}
            disabled={running}
            className="rounded-lg border border-[#3ddc84]/60 px-5 py-2.5 text-sm font-bold text-[#3ddc84] transition-all hover:bg-[#3ddc84]/10 disabled:cursor-not-allowed disabled:opacity-30"
          >
            练习模式
          </button>
          {running && (
            <button
              onClick={stopAll}
              className="rounded-lg border border-[#ff5a4e]/60 px-5 py-2.5 text-sm font-bold text-[#ff5a4e] transition-all hover:bg-[#ff5a4e]/10"
            >
              ■ 结束
            </button>
          )}
          <label className="ml-1 flex items-center gap-2 text-xs text-[#9aa0a8]">
            抽题数
            <select
              value={questionCount}
              onChange={(e) => setQuestionCount(Number(e.target.value))}
              disabled={running}
              className="rounded-md border border-[#33363c] bg-[#17181b] px-2 py-1.5 text-[#e8eaee]"
            >
              {[4, 5, 6, 7, 8].map((n) => (
                <option key={n} value={n}>
                  {n} 题
                </option>
              ))}
            </select>
          </label>
          <div className="ml-auto flex items-center gap-4 text-sm">
            <span className="text-[#3ddc84]">✓ {stats.ok}</span>
            <span className="text-[#ff5a4e]">✗ {stats.bad}</span>
            {progressText && <span className="text-[#9aa0a8]">{progressText}</span>}
          </div>
        </div>

        {/* 当前指令 + 倒计时 */}
        <div className="mb-5 flex items-center gap-5 rounded-2xl border border-[#222429] bg-gradient-to-r from-[#121316] to-[#0d0e10] px-5 py-4">
          {/* 倒计时环 */}
          <div className="relative h-16 w-16 shrink-0">
            <svg viewBox="0 0 64 64" className="h-full w-full -rotate-90">
              <circle cx="32" cy="32" r="27" fill="none" stroke="#22252a" strokeWidth="6" />
              <circle
                cx="32"
                cy="32"
                r="27"
                fill="none"
                stroke={countdown < 2000 && countdown > 0 ? '#ff5a4e' : '#3ddc84'}
                strokeWidth="6"
                strokeLinecap="round"
                strokeDasharray={2 * Math.PI * 27}
                strokeDashoffset={2 * Math.PI * 27 * (1 - ringPct)}
                style={{ transition: 'stroke-dashoffset 60ms linear' }}
              />
            </svg>
            <span
              className="absolute inset-0 flex items-center justify-center text-sm font-bold tabular-nums"
              style={{ color: countdown < 2000 && countdown > 0 ? '#ff5a4e' : '#e8eaee' }}
            >
              {phase === 'window' ? seconds : '—'}
            </span>
          </div>
          <div className="min-w-0 flex-1">
            <div className="text-[11px] uppercase tracking-[0.2em] text-[#6b7078]">
              {phase === 'speaking' ? '🔊 语音播报' : phase === 'window' ? '请操作' : running ? '…' : '当前指令'}
            </div>
            <div className="mt-1 truncate text-lg font-semibold sm:text-xl">
              {running
                ? currentStep?.prompt ?? ''
                : result
                  ? result.title
                  : '准备好后开始,先听语音指令,再操作灯光'}
            </div>
            {result && !running && <div className="mt-0.5 text-sm text-[#9aa0a8]">{result.detail}</div>}
          </div>
          <div className="hidden shrink-0 text-right text-xs text-[#7d838c] sm:block">
            当前灯光
            <div className="mt-1 text-sm font-medium text-[#c9cdd4]">{describeLights(lights)}</div>
          </div>
        </div>

        {/* 座舱区 */}
        <div className="grid gap-5 lg:grid-cols-[1fr_1.15fr]">
          {/* 左: 操作区 */}
          <div className="relative overflow-hidden rounded-[28px] border border-[#26282d] bg-gradient-to-b from-[#141518] via-[#0f1012] to-[#0a0b0d] p-6 shadow-[inset_0_1px_0_rgba(255,255,255,0.05),0_18px_40px_rgba(0,0,0,0.5)]">
            <div className="mb-2 text-center text-[11px] uppercase tracking-[0.25em] text-[#6b7078]">
              驾驶舱 · 灯光控制
            </div>
            <div className="flex flex-wrap items-center justify-center gap-x-10 gap-y-10 py-6">
              <LightKnob knob={lights.knob} pull={lights.pull} onKnob={onKnob} onPull={onPull} />
              <div className="flex flex-col items-center gap-8">
                {/* 双闪按钮 */}
                <button
                  onClick={onHazard}
                  className={`flex items-center gap-2 rounded-xl border px-4 py-2.5 text-sm font-bold transition-all ${
                    lights.hazard
                      ? 'border-[#ff5a4e] bg-[#ff5a4e]/15 text-[#ff5a4e] shadow-[0_0_16px_rgba(255,90,78,0.3)]'
                      : 'border-[#3a3d43] bg-[#1b1d20] text-[#c9cdd4] hover:border-[#4a4e55]'
                  }`}
                >
                  <span className={lights.hazard ? 'indicator-blink' : ''} style={{ color: '#ff5a4e' }}>
                    <HazardIcon size={20} />
                  </span>
                  危险报警闪光灯
                </button>
                <Stalk
                  turn={lights.turn}
                  highBeam={lights.highBeam}
                  flashActive={flashActive}
                  onTurn={onTurn}
                  onFlashDown={onFlashDown}
                  onFlashUp={onFlashUp}
                  onHighBeam={onHighBeam}
                />
              </div>
            </div>
            <p className="mt-4 text-center text-[11px] leading-5 text-[#565b63]">
              旋钮: 0 → 示宽灯 → 近光灯,向外拉一档前雾灯、二档后雾灯 ·
              拨杆: 上抬右转向 / 下压左转向 / 向外推远光 / 向怀里拉远近光交替
            </p>
          </div>

          {/* 右: 仪表盘 */}
          <Cluster lights={lights} flashActive={flashActive} lcdTitle={lcdTitle} lcdSub={lcdSub} lcdTone={lcdTone} />
        </div>

        {/* 题库 */}
        <div className="mt-6 rounded-2xl border border-[#222429] bg-[#101114]">
          <button
            onClick={() => setShowBank((v) => !v)}
            className="flex w-full items-center justify-between px-5 py-3.5 text-sm font-semibold text-[#c9cdd4]"
          >
            <span>考试题库与标准操作({BANK.length} 题)</span>
            <span className="text-[#6b7078]">{showBank ? '▲ 收起' : '▼ 展开'}</span>
          </button>
          {showBank && (
            <div className="grid gap-x-8 gap-y-1 border-t border-[#222429] px-5 py-4 sm:grid-cols-2">
              {BANK.map((q) => (
                <div key={q.id} className="flex items-baseline justify-between gap-3 border-b border-[#1a1c20] py-2 text-[13px]">
                  <span className="text-[#c9cdd4]">{q.prompt}</span>
                  <span className="shrink-0 text-right text-[#3ddc84]">{q.tip}</span>
                </div>
              ))}
            </div>
          )}
        </div>

        <p className="mt-5 text-center text-xs leading-6 text-[#565b63]">
          考试流程与真实科目三一致: 开场语 → 请开启前照灯 → 随机灯光指令(5 秒内作答) → 关闭所有灯光。
          <br />
          任意一项操作错误或超时即不合格; 练习模式答错会播报正确操作并继续。
        </p>
      </main>
    </div>
  )
}
