if (!Object.groupBy) Object.groupBy = (items, f) => { const o = Object.create(null); let i = 0; for (const x of items) { const k = f(x, i++); (o[k] ??= []).push(x) } return o }
if (!Map.groupBy) Map.groupBy = (items, f) => { const m = new Map(); let i = 0; for (const x of items) { const k = f(x, i++); if (!m.has(k)) m.set(k, []); m.get(k).push(x) } return m }
if (!Promise.withResolvers) Promise.withResolvers = () => { let resolve, reject; const promise = new Promise((a, b) => { resolve = a; reject = b }); return { promise, resolve, reject } }
