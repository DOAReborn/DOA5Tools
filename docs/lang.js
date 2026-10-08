(function () {
  var html = document.documentElement;
  var choix = localStorage.getItem("langue") ||
    ((navigator.language || "").toLowerCase().indexOf("fr") === 0 ? "fr" : "en");
  html.setAttribute("data-lang", choix);
  html.setAttribute("lang", choix);
  document.addEventListener("DOMContentLoaded", function () {
    var b = document.getElementById("langue");
    if (!b) return;
    b.addEventListener("click", function () {
      choix = choix === "fr" ? "en" : "fr";
      html.setAttribute("data-lang", choix);
      html.setAttribute("lang", choix);
      localStorage.setItem("langue", choix);
    });
  });
})();
