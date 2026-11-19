// Adaptateur StudyLab ↔ foliate-js (03-comparaison-bibliotheques §EPUB).
// Tout ce que l'application demande au lecteur passe par window.StudyLab ; tout ce que le lecteur
// signale à l'application passe par bridge.post(événement, JSON). L'API de foliate-js n'étant pas
// déclarée stable, elle n'est utilisée qu'ici.
import './polyfills.js'
import './foliate/view.js'
import { EPUB } from './foliate/epub.js'
import { Overlayer } from './foliate/overlayer.js'
import * as CFI from './foliate/epubcfi.js'

let bridge = null
let view = null
let book = null
let base = ''
let sizes = {}
let settings = {}
let lastRelocate = null
const annotations = new Map()   // cfi → { id, type, color }
const drawn = new Map()         // cfi → { range, doc, index }
let searchResults = []
let searchToken = 0

const post = (event, data) => { if (bridge) bridge.post(event, JSON.stringify(data ?? {})) }
const log = (...args) => console.log('[studylab]', ...args)

// ───── Chargement des fichiers du livre (servis par l'application, sans extraction) ─────
const get = path => new Promise(resolve => {
    const x = new XMLHttpRequest()
    x.open('GET', base + path.split('/').map(encodeURIComponent).join('/'))
    x.responseType = 'arraybuffer'
    x.onload = () => resolve(x.status === 200 || x.status === 0 ? x.response : null)
    x.onerror = () => resolve(null)
    x.send()
})
const loader = {
    loadText: async path => { const b = await get(path); return b ? new TextDecoder().decode(b) : null },
    loadBlob: async (path, type) => { const b = await get(path); return b ? new Blob([b], { type }) : null },
    getSize: path => sizes[path] ?? 0,
}

// ───── Présentation ─────
const fontStacks = {
    serif: "Georgia, 'Noto Serif', 'DejaVu Serif', 'Times New Roman', serif",
    sans: "Inter, 'Segoe UI', 'Noto Sans', 'DejaVu Sans', Arial, sans-serif",
}
const getCSS = s => {
    const font = fontStacks[s.font]
    return `
    @namespace epub "http://www.idpf.org/2007/ops";
    html { color: ${s.fg} !important; background: ${s.bg} !important; }
    body { color: ${s.fg} !important; background: transparent !important; font-size: ${s.fontSize}% !important; }
    ${font ? `body, p, li, blockquote, dd, dt, td, th, div, span, h1, h2, h3, h4, h5, h6 { font-family: ${font} !important; }` : ''}
    p, li, blockquote, dd { line-height: ${s.lineHeight} !important; }
    p { hyphens: auto; -webkit-hyphens: auto; widows: 2; orphans: 2; }
    a:any-link { color: ${s.link} !important; }
    img, svg, video { max-width: 100%; }
    ::selection { background: ${s.selection}; }
    pre { white-space: pre-wrap !important; }
    aside[epub|type~="footnote"], aside[epub|type~="endnote"] { display: none; }
    `
}
const applySettings = () => {
    const s = settings
    document.documentElement.style.setProperty('--bg', s.bg)
    document.documentElement.style.setProperty('--fg', s.fg)
    document.documentElement.style.setProperty('--muted', s.muted)
    if (!view?.renderer) return
    const r = view.renderer
    r.setAttribute('flow', s.flow === 'scrolled' ? 'scrolled' : 'paginated')
    r.setAttribute('gap', `${s.gap}%`)
    r.setAttribute('margin', '40px')
    r.setAttribute('max-inline-size', `${s.maxInlineSize}px`)
    r.setAttribute('max-column-count', String(s.columns ?? 2))
    r.setStyles?.(getCSS(s))
    document.documentElement.style.setProperty('--overlayer-highlight-opacity', s.dark ? '.38' : '.32')
    document.documentElement.style.setProperty('--overlayer-highlight-blend-mode', s.dark ? 'screen' : 'multiply')
}

// ───── Positions ─────
const sectionFraction = (doc, range) => {
    try {
        const r = doc.createRange()
        r.setStart(doc.body, 0)
        r.setEnd(range.startContainer, range.startOffset)
        const total = doc.body.textContent.length
        return total > 0 ? Math.min(0.999, r.toString().length / total) : 0
    } catch { return 0 }
}
const bookProgress = (index, fraction) => {
    const f = view.getSectionFractions()
    if (!f.length) return 0
    const start = f[index] ?? 0
    const end = f[index + 1] ?? 1
    return Math.max(0, Math.min(1, start + (end - start) * fraction))
}
// Rectangle (pixels CSS de la page principale) d'une plage d'un document de chapitre.
const rangeRect = (doc, range) => {
    const frame = doc.defaultView?.frameElement
    const off = frame ? frame.getBoundingClientRect() : { left: 0, top: 0 }
    const rects = Array.from(range.getClientRects())
        .map(r => ({ left: r.left + off.left, top: r.top + off.top, right: r.right + off.left, bottom: r.bottom + off.top }))
        .filter(r => r.right > 0 && r.left < window.innerWidth && r.bottom > 0 && r.top < window.innerHeight)
    if (!rects.length) return null
    const b = rects.reduce((a, r) => ({ left: Math.min(a.left, r.left), top: Math.min(a.top, r.top),
        right: Math.max(a.right, r.right), bottom: Math.max(a.bottom, r.bottom) }))
    return { x: b.left, y: b.top, w: b.right - b.left, h: b.bottom - b.top }
}
const context = (range, before) => {
    try {
        const doc = range.startContainer.ownerDocument
        const r = doc.createRange()
        if (before) { r.setStart(doc.body, 0); r.setEnd(range.startContainer, range.startOffset) }
        else { r.setStart(range.endContainer, range.endOffset); r.setEnd(doc.body, doc.body.childNodes.length) }
        const t = r.toString().replace(/\s+/g, ' ')
        return before ? t.slice(-32).trimStart() : t.slice(0, 32).trimEnd()
    } catch { return '' }
}

// ───── Sélection ─────
let selectionTimer = 0
const reportSelection = (doc, index) => {
    const sel = doc.getSelection()
    if (!sel || sel.isCollapsed || sel.rangeCount === 0) { post('selectionCleared'); return }
    const range = sel.getRangeAt(0)
    const text = range.toString()
    if (!text.trim()) { post('selectionCleared'); return }
    const fraction = sectionFraction(doc, range)
    post('selection', {
        cfi: view.getCFI(index, range),
        text, prefix: context(range, true), suffix: context(range, false),
        section: index, fraction, progress: bookProgress(index, fraction),
        rect: rangeRect(doc, range),
    })
}
const annotationAt = (doc, x, y) => {
    for (const [cfi, d] of drawn) {
        if (d.doc !== doc) continue
        for (const r of d.range.getClientRects())
            if (x >= r.left - 2 && x <= r.right + 2 && y >= r.top - 2 && y <= r.bottom + 2) return cfi
    }
    return null
}
const onKey = e => {
    if (e.ctrlKey || e.altKey || e.metaKey) return
    const k = e.key
    if (k === 'ArrowLeft' || k === 'h') { view.goLeft(); e.preventDefault() }
    else if (k === 'ArrowRight' || k === 'l') { view.goRight(); e.preventDefault() }
    else if (settings.flow !== 'scrolled' && (k === 'PageUp' || k === 'ArrowUp')) { view.prev(); e.preventDefault() }
    else if (settings.flow !== 'scrolled' && (k === 'PageDown' || k === 'ArrowDown' || k === ' ')) { view.next(); e.preventDefault() }
    else if (k === 'Escape') post('escape')
}
const onLoadSection = ({ doc, index }) => {
    doc.addEventListener('keydown', onKey)
    doc.addEventListener('pointerup', () => {
        clearTimeout(selectionTimer)
        selectionTimer = setTimeout(() => reportSelection(doc, index), 10)
    })
    doc.addEventListener('selectionchange', () => {
        const sel = doc.getSelection()
        if (!sel || sel.isCollapsed) post('selectionCleared')
    })
    doc.addEventListener('contextmenu', e => {
        const cfi = annotationAt(doc, e.clientX, e.clientY)
        const frame = doc.defaultView?.frameElement
        const off = frame ? frame.getBoundingClientRect() : { left: 0, top: 0 }
        e.preventDefault()
        post('contextMenu', { id: cfi ? annotations.get(cfi)?.id ?? 0 : 0, x: e.clientX + off.left, y: e.clientY + off.top })
    })
    // Rendu des annotations de ce chapitre (le chapitre vient d'être chargé)
    for (const cfi of annotations.keys()) view.addAnnotation({ value: cfi }).catch(() => {})
}

// ───── Annotations ─────
const drawAnnotation = ({ draw, annotation, doc, range }) => {
    const a = annotations.get(annotation.value)
    if (!a) return
    drawn.set(annotation.value, { range, doc })
    if (a.type === 'underline') draw(Overlayer.underline, { color: a.stroke, width: 2 })
    else if (a.type === 'strikeout') draw(Overlayer.strikethrough, { color: a.stroke, width: 2 })
    else if (a.type === 'bookmark') return
    else draw(Overlayer.highlight, { color: a.fill })
}
const setAnnotations = async list => {
    if (!view) return
    const next = new Map(list.map(a => [a.cfi, a]))
    for (const cfi of annotations.keys())
        if (!next.has(cfi) || annotations.get(cfi).type === 'bookmark') { drawn.delete(cfi); await view.deleteAnnotation({ value: cfi }).catch(() => {}) }
    annotations.clear()
    for (const [cfi, a] of next) annotations.set(cfi, a)
    for (const [cfi, a] of next) if (a.type !== 'bookmark') await view.addAnnotation({ value: cfi }).catch(() => {})
    reportBookmark()
}
const flash = async cfi => {
    const d = drawn.get(cfi)
    if (!d) return
    const a = annotations.get(cfi)
    for (let i = 0; i < 4; i++) {
        await new Promise(r => setTimeout(r, 300))
        if (i % 2 === 0) await view.deleteAnnotation({ value: cfi }).catch(() => {})
        else if (a) await view.addAnnotation({ value: cfi }).catch(() => {})
    }
}
// Signet sur la vue courante : existe-t-il un signet dans la plage visible ?
const visibleBookmark = () => {
    const cfi = lastRelocate?.cfi
    if (!cfi) return null
    const start = CFI.collapse(cfi)
    const end = CFI.collapse(cfi, true)
    for (const [value, a] of annotations) {
        if (a.type !== 'bookmark') continue
        try {
            if (CFI.compare(value, start) >= 0 && CFI.compare(value, end) <= 0) return a.id
        } catch { /* CFI illisible : ignorée */ }
    }
    return null
}
const reportBookmark = () => post('bookmarkState', { id: visibleBookmark() ?? 0 })

// ───── Recherche ─────
const search = async query => {
    const token = ++searchToken
    searchResults = []
    view.clearSearch()
    if (!query) { post('searchResults', { done: true, count: 0 }); return }
    for await (const r of view.search({ query, matchCase: false, matchDiacritics: false, matchWholeWords: false })) {
        if (token !== searchToken) return
        if (r === 'done') break
        if (r.subitems) for (const s of r.subitems) searchResults.push({ cfi: s.cfi, label: r.label, excerpt: s.excerpt })
        post('searchResults', { done: false, count: searchResults.length })
    }
    if (token === searchToken) post('searchResults', { done: true, count: searchResults.length })
}

// ───── Table des matières ─────
const flattenToc = (items, depth = 0, out = []) => {
    for (const item of items ?? []) {
        let section = -1
        try { section = book.resolveHref(item.href)?.index ?? -1 } catch { /* lien invalide */ }
        out.push({ label: (item.label ?? '').trim(), href: item.href ?? '', depth, section })
        flattenToc(item.subitems, depth + 1, out)
    }
    return out
}

// ───── API appelée par l'application ─────
const StudyLab = {
    async open(opts) {
        try {
            base = opts.base
            sizes = opts.sizes ?? {}
            settings = opts.settings ?? settings
            applySettings()
            book = await new EPUB(loader).init()
            view = document.createElement('foliate-view')
            document.body.append(view)
            view.addEventListener('load', e => onLoadSection(e.detail))
            view.addEventListener('relocate', e => {
                lastRelocate = e.detail
                const d = e.detail
                const r = view.renderer
                post('relocated', {
                    cfi: d.cfi, fraction: d.fraction ?? 0,
                    section: d.section?.current ?? 0, sections: d.section?.total ?? 0,
                    tocLabel: d.tocItem?.label?.trim() ?? '', tocHref: d.tocItem?.href ?? '',
                    page: r && !r.scrolled ? Math.max(1, r.page) : 0,
                    pages: r && !r.scrolled ? Math.max(1, r.pages - 2) : 0,
                    sectionMinutes: d.time?.section ?? 0,
                })
                reportBookmark()
            })
            view.addEventListener('draw-annotation', e => drawAnnotation(e.detail))
            view.addEventListener('show-annotation', e => {
                const a = annotations.get(e.detail.value)
                const d = drawn.get(e.detail.value)
                if (a) post('annotationClicked', { id: a.id, rect: d ? rangeRect(d.doc, d.range) : null })
            })
            view.addEventListener('link', () => post('linkFollowed'))
            view.addEventListener('external-link', e => { e.preventDefault(); post('externalLink', { href: e.detail.href_ }) })
            await view.open(book)
            applySettings()
            document.addEventListener('keydown', onKey)
            const lastLocation = opts.lastLocation || null
            await view.init({ lastLocation, showTextStart: !lastLocation })
            post('opened', {
                title: book.metadata?.title ?? '',
                sections: book.sections.length,
                toc: flattenToc(book.toc),
                rtl: book.dir === 'rtl',
            })
        } catch (e) {
            console.error(e)
            post('error', { message: String(e?.message ?? e) })
        }
    },
    setSettings(s) { settings = s; applySettings() },
    goTo(target) { return view?.goTo(target) },
    goToFraction(f) { return view?.goToFraction(f) },
    next() { return view?.next() },
    prev() { return view?.prev() },
    goLeft() { return view?.goLeft() },
    goRight() { return view?.goRight() },
    back() { view?.history.back() },
    forward() { view?.history.forward() },
    setAnnotations,
    async reveal(cfi) {
        if (!view) return
        await view.goTo(cfi)
        if (drawn.has(cfi)) flash(cfi)
    },
    rectOf(cfi) {
        const d = drawn.get(cfi)
        post('annotationRect', { cfi, rect: d ? rangeRect(d.doc, d.range) : null })
    },
    clearSelection() { view?.deselect() },
    selectedText() {
        for (const { doc } of view?.renderer?.getContents() ?? []) {
            const t = doc.getSelection()?.toString()
            if (t) return t
        }
        return ''
    },
    search,
    clearSearch() { searchToken++; searchResults = []; view?.clearSearch() },
    async showResult(i) {
        const r = searchResults[i]
        if (r) await view.goTo(r.cfi)
    },
    // État courant, pour les tests et le signet
    state() {
        const d = lastRelocate
        return { cfi: d?.cfi ?? '', fraction: d?.fraction ?? 0, section: d?.section?.current ?? 0,
                 progress: d ? bookProgress(d.section?.current ?? 0, 0) : 0,
                 drawn: Array.from(drawn.keys()), annotations: annotations.size }
    },
    // Ancre ponctuelle du début de la vue courante (signet)
    currentAnchor() {
        const d = lastRelocate
        if (!d?.range) return null
        const index = d.section?.current ?? 0
        const r = d.range.cloneRange()
        r.collapse(true)
        const doc = r.startContainer.ownerDocument
        const fraction = sectionFraction(doc, r)
        const t = d.range.toString().replace(/\s+/g, ' ').trim()
        return { cfi: view.getCFI(index, r), section: index, fraction, progress: bookProgress(index, fraction),
                 text: t.slice(0, 120) }
    },
    // Tests : sélectionne le n-ième passage contenant `needle` dans le chapitre affiché et le signale
    selectText(needle) {
        for (const { doc, index } of view?.renderer?.getContents() ?? []) {
            const walker = doc.createTreeWalker(doc.body, NodeFilter.SHOW_TEXT)
            for (let n = walker.nextNode(); n; n = walker.nextNode()) {
                const at = n.data.indexOf(needle)
                if (at < 0) continue
                const range = doc.createRange()
                range.setStart(n, at)
                range.setEnd(n, at + needle.length)
                const sel = doc.getSelection()
                sel.removeAllRanges()
                sel.addRange(range)
                reportSelection(doc, index)
                return true
            }
        }
        return false
    },
}
window.StudyLab = StudyLab

// ───── Canal avec l'application ─────
const start = () => {
    if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined') {
        log('canal Qt indisponible')
        return
    }
    new QWebChannel(qt.webChannelTransport, channel => {
        bridge = channel.objects.bridge
        post('ready')
    })
}
start()
