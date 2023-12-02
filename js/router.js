/*
    declare the reload function below
*/
function upload_reload() {
    fetch('/config')
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.json();
        })
        .then(data => {
            document.getElementById('file_max_size').textContent += (data.file_max_byte / 1048576).toFixed(2) + ' MB';;
            document.getElementById('file_expire_in').textContent += (data.file_expire / 60) + ' Hours';
        })
        .catch(error => {
            // Handle any errors
            console.error('There has been a problem with your fetch operation:', error);
        });

    var dropZone = document.getElementById('drop_zone_container');

    dropZone.addEventListener('dragover', function (e) {
        e.preventDefault();
        e.stopPropagation();
        dropZone.classList.add('over');
    });

    dropZone.addEventListener('dragleave', function (e) {
        e.preventDefault();
        e.stopPropagation();
        dropZone.classList.remove('over');
    });

    dropZone.addEventListener('drop', function (e) {
        e.preventDefault();
        e.stopPropagation();
        dropZone.classList.remove('over');

        var files = e.dataTransfer.files;
        if (files.length > 0) {
            var formData = new FormData();
            formData.append('file', files[0]);

            var xhr = new XMLHttpRequest();
            xhr.open('POST', '/', true);
            xhr.onload = function () {
                if (xhr.status === 200) {
                    alert('File uploaded successfully!');
                } else {
                    alert('Upload error!');
                }
            };
            xhr.send(formData);
        }
    });
}

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
    const path = window.location.pathname;
    const route = routes[path] || routes[404];
    const html = await fetch(route).then((data) => data.text());
    document.getElementById("main-page").innerHTML = html;

    if (path == '/upload') {
        upload_reload()
    }
};

window.onpopstate = handleLocation;
window.route = route;

handleLocation();