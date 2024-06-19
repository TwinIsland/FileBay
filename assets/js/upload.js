/*
    declare the reload function below
*/

fetchConfig().then(config => {
    if (config) {
        document.getElementById('file_max_size').textContent += (config.file_max_byte / 1048576).toFixed(2) + ' MB';;
        document.getElementById('file_expire_in').textContent += (config.file_expire / 60).toFixed(2) + ' Hours';
    }
});

var dropZone = document.getElementById('drop_zone_container');

function upload_handler(formData) {

    var f = formData.get('file'), r = new FileReader();
    r.readAsArrayBuffer(f);
    r.onload = async function () {
        sendFileData(f.name, new Uint8Array(r.result), 2000000, 114514);
    }
}

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
    var formData = new FormData();


    var files = e.dataTransfer.files;
    if (files.length > 0) {
        droppedFile = files[0]; // Store the dropped file
        const fileNameDisplay = document.getElementById('fileNameDisplay');
        formData.append('file', droppedFile); // Use the dropped file

        upload_handler(formData);
    }
});


document.getElementById('uploadForm').addEventListener('submit', function (e) {
    e.preventDefault(); // Prevent the default form submission

    var formData = new FormData();
    formData.append('file', this.file.files[0]);

    upload_handler(formData);
});

var statusText = document.getElementById('status-text');
var statusDot = document.getElementById('status-dot');
var fileInputField = document.getElementById("fileInput");
var fileSubmitField = document.getElementById("submit_btn");
var ws = new WebSocket('ws://localhost:1234/api/status');

ws.onopen = () => {
    statusText.innerText = 'Connected';
};

ws.onmessage = (event) => {
    if (parseInt(event.data) == 1) {
        fileInputField.disabled = true;
        fileSubmitField.disabled = true;
        statusText.innerText = 'Service Busy';
        statusDot.style.backgroundColor = 'red';
    } else {
        fileInputField.disabled = false;
        fileSubmitField.disabled = false;
        statusText.innerText = 'Service Available';
        statusDot.style.backgroundColor = 'green';
    }
};

ws.onerror = () => {
    fileInputField.disabled = true;
    fileSubmitField.disabled = true;
    statusText.innerText = 'Server Error';
    statusDot.style.backgroundColor = 'gray';
};

ws.onclose = () => {
    statusText.innerText = 'Connection Closed';
    statusDot.style.backgroundColor = 'gray';
};


// Cleanup function
window.currentCleanup = function () {
    ws.close();
    return "/ws listeners";
}