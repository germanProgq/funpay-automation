/*
Purpose: Scroll reveals, parallax accents, nav toggle, and console estimate updates.
*/
const prefersReducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

const navToggle = document.querySelector("[data-nav-toggle]");
const nav = document.querySelector("[data-nav]");

if (navToggle && nav) {
  navToggle.addEventListener("click", () => {
    const isOpen = document.body.classList.toggle("nav-open");
    navToggle.setAttribute("aria-expanded", String(isOpen));
  });

  nav.addEventListener("click", (event) => {
    if (event.target instanceof HTMLAnchorElement && document.body.classList.contains("nav-open")) {
      document.body.classList.remove("nav-open");
      navToggle.setAttribute("aria-expanded", "false");
    }
  });
}

const staggerGroups = document.querySelectorAll("[data-stagger]");
staggerGroups.forEach((group) => {
  const children = group.querySelectorAll(":scope > *");
  children.forEach((child, index) => {
    child.style.setProperty("--stagger", String(index));
  });
});

const revealElements = document.querySelectorAll("[data-reveal]");

if (prefersReducedMotion) {
  revealElements.forEach((element) => element.classList.add("is-visible"));
} else if (revealElements.length > 0) {
  const observer = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          entry.target.classList.add("is-visible");
          observer.unobserve(entry.target);
        }
      });
    },
    {
      threshold: 0.15,
      rootMargin: "0px 0px -10% 0px",
    }
  );

  revealElements.forEach((element) => observer.observe(element));
}

const progressBar = document.querySelector("[data-scroll-progress]");
const parallaxElements = document.querySelectorAll("[data-parallax]");
let scrollTicking = false;

const updateScroll = () => {
  const scrollTop = window.scrollY || window.pageYOffset;
  const maxScroll = document.documentElement.scrollHeight - window.innerHeight;
  const progress = maxScroll > 0 ? scrollTop / maxScroll : 0;

  if (progressBar) {
    progressBar.style.transform = `scaleX(${progress})`;
  }

  parallaxElements.forEach((element) => {
    const factor = Number.parseFloat(element.dataset.parallax || "0");
    element.style.setProperty("--parallax-shift", `${scrollTop * factor}px`);
  });

  scrollTicking = false;
};

if (!prefersReducedMotion && (progressBar || parallaxElements.length > 0)) {
  window.addEventListener("scroll", () => {
    if (!scrollTicking) {
      scrollTicking = true;
      window.requestAnimationFrame(updateScroll);
    }
  });
  window.addEventListener("resize", updateScroll);
  updateScroll();
} else if (progressBar) {
  progressBar.style.transform = "scaleX(1)";
}

const lotInput = document.querySelector("[data-lots]");
const delayInput = document.querySelector("[data-delay]");
const lotValue = document.querySelector("[data-lots-value]");
const delayValue = document.querySelector("[data-delay-value]");
const responseValue = document.querySelector("[data-response]");
const cycleValue = document.querySelector("[data-cycle]");
const alertsValue = document.querySelector("[data-alerts]");
const modeInputs = document.querySelectorAll("input[name='mode']");

const modeFactors = {
  instant: 1,
  queued: 1.2,
  review: 1.4,
};

const numberFormatter = new Intl.NumberFormat("en-US");

const updateConsole = () => {
  if (!lotInput || !delayInput || !lotValue || !delayValue || !responseValue || !cycleValue || !alertsValue) {
    return;
  }

  const lots = Number.parseInt(lotInput.value, 10);
  const delay = Number.parseInt(delayInput.value, 10);
  const modeSelection = document.querySelector("input[name='mode']:checked");
  const modeKey = modeSelection ? modeSelection.value : "instant";
  const factor = modeFactors[modeKey] ?? 1;
  const response = Math.max(1, Math.round(delay * factor));
  const cycle = Math.max(15, Math.round(35 + lots / 12 + (factor - 1) * 12));
  const alerts = Math.max(1, Math.round(lots * 0.18));

  lotValue.textContent = `${numberFormatter.format(lots)} lots`;
  delayValue.textContent = `${numberFormatter.format(delay)} sec`;
  responseValue.textContent = `${response} sec`;
  cycleValue.textContent = `${cycle} min`;
  alertsValue.textContent = `${alerts} / day`;
};

if (lotInput && delayInput) {
  lotInput.addEventListener("input", updateConsole);
  delayInput.addEventListener("input", updateConsole);
  modeInputs.forEach((input) => input.addEventListener("change", updateConsole));
  updateConsole();
}
