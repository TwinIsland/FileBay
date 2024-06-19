
/*
    single page app utils
*/
const route = (event) => {
    event = event || window.event;
    event.preventDefault();
    window.history.pushState({}, "", event.target.href);
    handleLocation();
};

const routes = {
    404: "/pages/404.html",
    "/": "/pages/index.html",
    "/about": "/pages/about.html",
    "/upload": "/pages/upload.html",
};



const handleLocation = async () => {
    // cleanup the previous page renderer
    if (window.currentCleanup) {
        const cleaner_name = window.currentCleanup();
        window.currentCleanup = null;
        console.log("clean: " + cleaner_name)
    }

    const path = window.location.pathname;

    const route = routes[path] || routes[404];
    const html = await fetch(route).then((data) => data.text());
    document.getElementById("main-page").innerHTML = html;

    if (path == '/upload') {
        const script = document.createElement("script");
        script.src = "/js/upload.js";
        script.className = 'route-script';
        document.body.appendChild(script);
    }
};

window.onpopstate = handleLocation;
window.route = route;

handleLocation();