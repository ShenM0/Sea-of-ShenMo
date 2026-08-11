import { useCallback, useEffect, useRef, useState } from 'react'

/** 中文语音播报(浏览器 speechSynthesis) */
export function useSpeech() {
  const [enabled, setEnabled] = useState(true)
  const [rate, setRate] = useState(0.95)
  const voiceRef = useRef<SpeechSynthesisVoice | null>(null)
  const supported = typeof window !== 'undefined' && 'speechSynthesis' in window

  useEffect(() => {
    if (!supported) return
    const pick = () => {
      const voices = window.speechSynthesis.getVoices()
      voiceRef.current =
        voices.find((v) => /zh[-_]CN/i.test(v.lang) && /xiaoxiao|yunjian|ting|mei|hui/i.test(v.name)) ||
        voices.find((v) => /zh[-_]CN/i.test(v.lang)) ||
        voices.find((v) => /^zh/i.test(v.lang)) ||
        null
    }
    pick()
    window.speechSynthesis.onvoiceschanged = pick
    return () => {
      window.speechSynthesis.cancel()
    }
  }, [supported])

  const speak = useCallback(
    (text: string, onend?: () => void) => {
      if (!supported || !enabled) {
        // 无语音时按字数估算播报时长,保证考试节奏一致
        const est = Math.min(6000, 600 + text.length * 260)
        const t = window.setTimeout(() => onend?.(), est)
        return () => window.clearTimeout(t)
      }
      window.speechSynthesis.cancel()
      const u = new SpeechSynthesisUtterance(text)
      u.lang = 'zh-CN'
      u.rate = rate
      u.pitch = 1
      if (voiceRef.current) u.voice = voiceRef.current
      let done = false
      const finish = () => {
        if (!done) {
          done = true
          onend?.()
        }
      }
      u.onend = finish
      u.onerror = finish
      window.speechSynthesis.speak(u)
      // 部分浏览器 onend 不触发的兜底
      const est = Math.min(12000, 800 + text.length * (320 / rate))
      const t = window.setTimeout(finish, est)
      return () => {
        window.clearTimeout(t)
        u.onend = null
        u.onerror = null
      }
    },
    [enabled, rate, supported],
  )

  const stop = useCallback(() => {
    if (supported) window.speechSynthesis.cancel()
  }, [supported])

  return { speak, stop, enabled, setEnabled, rate, setRate, supported }
}

/** 转向灯/双闪 "哒哒" 声 (WebAudio) */
export function useBlinkerSound(active: boolean) {
  const ctxRef = useRef<AudioContext | null>(null)
  useEffect(() => {
    if (!active) return
    let stopped = false
    if (!ctxRef.current) {
      try {
        ctxRef.current = new AudioContext()
      } catch {
        return
      }
    }
    const ctx = ctxRef.current
    ctx.resume().catch(() => {})
    let count = 0
    const tick = () => {
      if (stopped) return
      const osc = ctx.createOscillator()
      const gain = ctx.createGain()
      osc.type = 'square'
      osc.frequency.value = count % 2 === 0 ? 1400 : 1100
      gain.gain.setValueAtTime(0.04, ctx.currentTime)
      gain.gain.exponentialRampToValueAtTime(0.0001, ctx.currentTime + 0.04)
      osc.connect(gain).connect(ctx.destination)
      osc.start()
      osc.stop(ctx.currentTime + 0.05)
      count++
    }
    tick()
    const id = window.setInterval(tick, 460)
    return () => {
      stopped = true
      window.clearInterval(id)
    }
  }, [active])
}
