const SHOW_DELAY = 280;
const GAP = 8;

let layer: HTMLDivElement | null = null;
let current: HTMLElement | null = null;
let hovered: HTMLElement | null = null;
let timer = 0;

function tipLayer(): HTMLDivElement {
  if (!layer) {
    layer = document.createElement("div");
    layer.className = "app-tooltip hidden";
    layer.setAttribute("role", "tooltip");
    document.body.append(layer);
  }
  return layer;
}

function hideTip(): void {
  window.clearTimeout(timer);
  timer = 0;
  current = null;
  layer?.classList.add("hidden");
}

export type TipPlacement = "top" | "bottom";

function showTip(target: HTMLElement): void {
  const text = target.dataset.tip;
  if (!text) {
    hideTip();
    return;
  }
  const box = tipLayer();
  box.textContent = text;
  box.classList.remove("hidden");
  box.style.left = "0px";
  box.style.top = "0px";

  const anchor = target.getBoundingClientRect();
  const size = box.getBoundingClientRect();
  const left = Math.max(
    GAP,
    Math.min(anchor.left + anchor.width / 2 - size.width / 2, window.innerWidth - size.width - GAP),
  );
  const above = anchor.top - size.height - GAP;
  const below = anchor.bottom + GAP;
  const top =
    target.dataset.tipPlacement === "bottom"
      ? below + size.height <= window.innerHeight - GAP
        ? below
        : Math.max(GAP, above)
      : above >= GAP
        ? above
        : Math.min(below, window.innerHeight - size.height - GAP);
  box.style.left = `${left}px`;
  box.style.top = `${Math.max(GAP, top)}px`;
  current = target;
}

export function setTip(
  element: HTMLElement,
  text: string | null | undefined,
  placement?: TipPlacement,
): void {
  if (text) {
    element.dataset.tip = text;
    if (placement === "bottom") {
      element.dataset.tipPlacement = "bottom";
    } else {
      delete element.dataset.tipPlacement;
    }
    if (current === element && layer && !layer.classList.contains("hidden")) {
      showTip(element);
    }
  } else {
    delete element.dataset.tip;
    delete element.dataset.tipPlacement;
    if (current === element) hideTip();
  }
}

export function initTooltips(): void {
  const dismiss = (): void => {
    hovered = null;
    hideTip();
  };

  // 禁用的按钮不派发指针事件，命中测试才能拿到它自己身上的提示。
  document.addEventListener("pointermove", (ev) => {
    const hit = document.elementFromPoint(ev.clientX, ev.clientY)?.closest<HTMLElement>("[data-tip]") ?? null;
    if (hit === hovered) return;
    hovered = hit;
    window.clearTimeout(timer);
    hideTip();
    if (hit) {
      const target = hit;
      timer = window.setTimeout(() => showTip(target), SHOW_DELAY);
    }
  });

  document.addEventListener("pointerleave", dismiss);
  document.addEventListener("pointerdown", dismiss);
  document.addEventListener("focusin", (ev) => {
    const target = ev.target instanceof Element ? ev.target.closest<HTMLElement>("[data-tip]") : null;
    if (!target) return;
    window.clearTimeout(timer);
    showTip(target);
  });
  document.addEventListener("focusout", dismiss);
  document.addEventListener("keydown", (ev) => {
    if (ev.key === "Escape") dismiss();
  });
  document.addEventListener("scroll", dismiss, true);
  window.addEventListener("resize", dismiss);
  window.addEventListener("blur", dismiss);
}
