/*
    declare the reload function below
*/
function upload_reload() {

    function fetchConfig() {
        const itemStr = localStorage.getItem("serverConfig");
        if (itemStr) {
            const item = JSON.parse(itemStr);
            if ((new Date()).getTime() >= item.expire) {
                // If the item is expired, remove it and return null or fetch new data
                localStorage.removeItem("serverConfig");
                return fetchNewConfig();
            }
            return Promise.resolve(item);
        } else {
            // If there is no item, fetch new data
            return fetchNewConfig();
        }
    }

    function fetchNewConfig() {
        return fetch('/config')
            .then(response => {
                if (!response.ok) {
                    throw new Error('Network response was not ok');
                }
                return response.json();
            })
            .then(data => {
                const item = {
                    file_max_byte: data.file_max_byte,
                    file_expire: data.file_expire,
                    expire: (new Date()).getTime() + 24 * 60 * 60 * 1000,
                };
                localStorage.setItem("serverConfig", JSON.stringify(item));
                return item;
            })
            .catch(error => {
                console.error('There has been a problem with your fetch operation:', error);
            });
    }

    fetchConfig().then(config => {
        if (config) {
            document.getElementById('file_max_size').textContent += (config.file_max_byte / 1048576).toFixed(2) + ' MB';;
            document.getElementById('file_expire_in').textContent += (config.file_expire / 60).toFixed(2) + ' Hours';
        }
    });

    var dropZone = document.getElementById('drop_zone_container');

    function showUploadSuccess(code) {
        var dropZoneContainer = document.getElementById('drop_zone_container');
        var contentContainer = document.getElementsByClassName('content-container')[0]
        if (dropZoneContainer) {
            // Remove the drop zone container
            dropZoneContainer.parentNode.removeChild(dropZoneContainer);

            var htmlContent = `
            <hr>
            <h1 style="color:green">Congratulation!</h1>
            <p>Your file has been upload successfully!</p>
            <p>Use the code <span style="font-size: 20px;font-weight: bolder;">${code}</span> to pick it up</p>
            <img src="https://cirno.me/usr/themes/VOID/assets/suica_chara.gif">
            <hr>
            <p>Power By <a href="https://cirno.me">Cirno.me</a></p>
            `;

            contentContainer.innerHTML += htmlContent;
        }
    }

    function showUploadFailed(msg) {
        var dropZoneContainer = document.getElementById('drop_zone_container');
        var contentContainer = document.getElementsByClassName('content-container')[0]
        if (dropZoneContainer) {
            // Remove the drop zone container
            dropZoneContainer.parentNode.removeChild(dropZoneContainer);

            var htmlContent = `
            <hr>
            <h1 style="color:red">Errorrr!</h1>
            <p>Failed due to: <span style="font-size: 20px;font-weight: bolder;">${msg}</span></p>
            <img src="https://cirno.me/usr/themes/VOID/assets/suica_chara.gif">
            <hr>
            <p>Power By <a href="https://cirno.me">Cirno.me</a></p>
            `;

            contentContainer.innerHTML += htmlContent;
        }
    }

    function upload_handler(formData) {

        // Check if file size exceeds the limit
        var file = formData.get('file');
        fetchConfig().then(config => {
            // if (config && file && file.size > config.file_max_byte) {
            //     alert('File is too large');
            //     return;
            // }

            var xhr = new XMLHttpRequest();
            xhr.open('POST', '/upload', true);

            xhr.onload = function () {
                if (xhr.status === 200) {
                    // Handle the response here
                    var response = JSON.parse(xhr.responseText);
                    if (response.status === "ok") {
                        showUploadSuccess(response.code)
                    } else {
                        if (!response.msg) {
                            response.msg = "unknown reason";
                        }
                        showUploadFailed(response.msg)
                    }
                } else
                    // Handle non-200 responses
                    try {
                        var errorResponse = JSON.parse(xhr.responseText);
                        var errMessage = errorResponse.msg || "Unknown error occurred";
                        alert('Upload error: ' + errMessage);
                    } catch (e) {
                        // If response is not JSON or can't be parsed
                        alert('Upload error: Unable to parse error message');
                    }
            };

            xhr.onerror = function () {
                // Handle network errors
                alert('Upload failed: Network Error');
            };

            xhr.send(formData);
        });

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
}
