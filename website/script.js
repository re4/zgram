document.documentElement.dataset.ready = "true";

const header = document.querySelector(".site-header");
const menuButton = document.querySelector(".menu-toggle");
const mobileMenu = document.querySelector(".mobile-menu");
const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

const closeMenu = () => {
  if (!menuButton || !mobileMenu) return;
  menuButton.setAttribute("aria-expanded", "false");
  mobileMenu.hidden = true;
};

menuButton?.addEventListener("click", () => {
  const willOpen = menuButton.getAttribute("aria-expanded") !== "true";
  menuButton.setAttribute("aria-expanded", String(willOpen));
  mobileMenu.hidden = !willOpen;
});

mobileMenu?.querySelectorAll("a").forEach((link) => {
  link.addEventListener("click", closeMenu);
});

window.addEventListener("scroll", () => {
  header?.classList.toggle("is-scrolled", window.scrollY > 92);
}, { passive: true });

document.querySelectorAll("[data-dynamic-url]").forEach((meta) => {
  const path = meta.getAttribute("content");
  if (path) meta.setAttribute("content", new URL(path, window.location.href).href);
});

document.querySelectorAll("[data-year]").forEach((node) => {
  node.textContent = String(new Date().getFullYear());
});

const tabs = [...document.querySelectorAll("[data-panel]")];
const panels = [...document.querySelectorAll("[data-feature-panel]")];

const selectPanel = (name) => {
  tabs.forEach((tab) => {
    const selected = tab.dataset.panel === name;
    tab.setAttribute("aria-selected", String(selected));
    tab.tabIndex = selected ? 0 : -1;
  });
  panels.forEach((panel) => {
    panel.hidden = panel.dataset.featurePanel !== name;
  });
};

tabs.forEach((tab, index) => {
  tab.addEventListener("click", () => selectPanel(tab.dataset.panel));
  tab.addEventListener("keydown", (event) => {
    if (event.key !== "ArrowRight" && event.key !== "ArrowLeft") return;
    event.preventDefault();
    const direction = event.key === "ArrowRight" ? 1 : -1;
    const next = tabs[(index + direction + tabs.length) % tabs.length];
    next.focus();
    selectPanel(next.dataset.panel);
  });
});

const initiallySelected = tabs.find((tab) => tab.getAttribute("aria-selected") === "true");
if (initiallySelected) selectPanel(initiallySelected.dataset.panel);

const revealTargets = document.querySelectorAll("[data-reveal]");

if (reduceMotion.matches || !("IntersectionObserver" in window)) {
  revealTargets.forEach((target) => target.classList.add("is-visible"));
} else {
  const revealObserver = new IntersectionObserver((entries, observer) => {
    entries.forEach((entry) => {
      if (!entry.isIntersecting) return;
      entry.target.classList.add("is-visible");
      observer.unobserve(entry.target);
    });
  }, { threshold: 0.13 });

  revealTargets.forEach((target) => revealObserver.observe(target));
}
