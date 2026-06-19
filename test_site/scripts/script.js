/* HTTP Daemon Test Site - JavaScript */
'use strict';

(function () {
    /* Log page load time to the console */
    var loadTime = new Date();
    console.log('Page loaded at: ' + loadTime.toISOString());

    /* Highlight broken images */
    function markBrokenImages() {
        var images = document.querySelectorAll('img');
        images.forEach(function (img) {
            if (!img.complete || img.naturalWidth === 0) {
                img.style.outline = '3px solid red';
                img.title = 'Image failed to load: ' + img.src;
            }
        });
    }

    /* Add current date/time to footer */
    function updateFooter() {
        var footer = document.querySelector('footer');
        if (footer) {
            var span = document.createElement('span');
            span.id = 'js-timestamp';
            span.textContent = ' | Page rendered: ' + loadTime.toLocaleString();
            footer.appendChild(span);
        }
    }

    /* Run after DOM is ready */
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', function () {
            markBrokenImages();
            updateFooter();
        });
    } else {
        markBrokenImages();
        updateFooter();
    }

    /* Simple event: highlight nav link for current page */
    (function highlightCurrentNav() {
        var links = document.querySelectorAll('nav a');
        var path = window.location.pathname;
        links.forEach(function (link) {
            if (link.getAttribute('href') === path ||
                path.endsWith(link.getAttribute('href'))) {
                link.style.fontWeight = 'bold';
                link.style.color = '#aad4ff';
            }
        });
    })();
})();
