function appendNumber(number) {
    var display = document.getElementById('number-display');
    if (display.value.length < 6) {
        display.value += number;
    } else {
        display.style.borderColor = 'red'; // Change border color to red if display is empty
        setTimeout(function () {
            display.style.borderColor = ''; // Reset border color to default after some time
        }, 500); // Reset after 500ms
    }
}

function backspace() {
    var display = document.getElementById('number-display');
    if (display.value.length > 0) {
        display.value = display.value.slice(0, -1); // Remove the last character
        display.style.borderColor = ''; // Reset border color to default or as per your CSS
    }
}

function enter() {
    var display = document.getElementById('number-display');    

    fetch('/api/download?pass=' + display.value)
        .then(response => {
            if (response.ok) {
                display.style.borderColor = 'green';
                // Extract filename from Content-Disposition header
                const disposition = response.headers.get('Content-Disposition');
                let filename = 'downloadedFile';
                if (disposition && disposition.indexOf('attachment') !== -1) {
                    const filenameRegex = /filename[^;=\n]*=((['"]).*?\2|[^;\n]*)/;
                    const matches = filenameRegex.exec(disposition);
                    if (matches != null && matches[1]) {
                        filename = matches[1].replace(/['"]/g, '');
                    }
                }

                return response.blob().then(blob => ({ blob, filename }));
            } else {
                throw new Error('Network response was not ok');
            }
        })
        .then(({ blob, filename }) => {
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.style.display = 'none';
            a.href = url;
            a.download = filename;
            document.body.appendChild(a);
            a.click();
            window.URL.revokeObjectURL(url);
        })
        .catch(error => {
            console.error('Error:', error);
            display.style.borderColor = 'red';
            setTimeout(() => {
                display.style.borderColor = '';
            }, 500); // Reset border color after 500ms
        });
}

// keyboard supervise 
document.addEventListener('keydown', function (event) {
    if (event.key >= '0' && event.key <= '9') {
        // Numeric key is pressed
        appendNumber(event.key);
    } else if (event.key === 'Backspace') {
        // Backspace key is pressed
        backspace();
    } else if (event.key === 'Enter') {
        // Enter key is pressed
        enter();
    }
});

// common function
async function fetchConfig() {
    return fetch('/api/config')
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
            return item;
        })
        .catch(error => {
            console.error('There has been a problem with your fetch operation:', error);
        });
}

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
        <div id="dlink"></div>
        <hr>
        <p>Power By <a href="https://cirno.me">Cirno.me</a></p>
        `;

        contentContainer.innerHTML += htmlContent;

        new QRCode(document.getElementById("dlink"), {
            text: window.location.host + "/api/download?code=" + code,
            width: 180,
            height: 180,
            colorDark: "#B98A82",
            colorLight : "#f7f7f7",
            correctLevel: QRCode.CorrectLevel.H
        });
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
        <img src="suika.webp">
        <hr>
        <p>Power By <a href="https://cirno.me">Cirno.me</a></p>
        `;

        contentContainer.innerHTML += htmlContent;
    }
}

function sendFileData(name, data, chunkSize) {
    var sid;

    // Step 1: Request /api/apply to get sid
    fetch('/api/apply')
        .then(res => res.json())
        .then(response => {
            if (response.status == 1) {
                sid = response.code;
                sendChunk(0);
            } else {
                showUploadFailed(response.code);
            }
        })
        .catch(error => {
            showUploadFailed(error.code);
        });

    // Function to send a chunk
    var sendChunk = function (offset) {
        var chunk = data.subarray(offset, offset + chunkSize) || '';
        var url = '/api/upload?offset=' + offset + '&sid=' + sid;

        var opts = {
            method: 'POST',
            body: chunk
        };

        fetch(url, opts)
            .then(async function (res) {
                const response = await res.json();
                return ({ ok: res.ok, response });
            })
            .then(({ ok, response }) => {
                if (!ok) {
                    showUploadFailed(response);
                } else {
                    if (chunk.length > 0 && offset + chunk.length < data.length) {
                        sendChunk(offset + chunk.length);
                    } else if (offset + chunk.length >= data.length) {
                        finalizeUpload(name);
                    }
                }
            })
            .catch(error => {
                showUploadFailed(error);
            });
    };

    // Step 3: Finalize upload
    var finalizeUpload = function (filename) {
        fetch('/api/finalizer?sid=' + sid + '&file=' + filename)
            .then(res => res.json())
            .then(response => {
                if (response.status == 1) {
                    showUploadSuccess(response.code);
                } else {
                    showUploadFailed(response.code);
                }
            })
            .catch(error => {
                showUploadFailed(error.message);
            });
    };
}
