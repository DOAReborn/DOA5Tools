(function () {
  var hero = document.querySelector(".hero");
  if (hero && !window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    hero.addEventListener("mousemove", function (e) {
      hero.style.setProperty("--px", (e.clientX / innerWidth - .5) * -16 + "px");
      hero.style.setProperty("--py", (e.clientY / innerHeight - .5) * -12 + "px");
    });
  }
})();
