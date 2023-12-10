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
    console.log("value: " + display.value);

    fetch('/file/' + display.value)
        .then(response => {
            if (response.ok) {
                display.style.borderColor = 'green';
                // Extract filename from Content-Disposition header
                const contentDisposition = response.headers.get('Content-Disposition');
                const match = /filename="([^"]+)"/.exec(contentDisposition);
                const filename = match ? match[1] : display.value; // Default to display.value if filename not found
                return response.blob().then(blob => ({ blob, filename }));
            } else {
                throw new Error('File not found');
            }
        })
        .then(({ blob, filename }) => {
            // Create a URL for the blob
            let url = window.URL.createObjectURL(blob);

            // Create a temporary anchor element
            let a = document.createElement('a');
            a.style.display = 'none';
            a.href = url;
            a.download = filename; // Set the filename

            // Append to the document and click it
            document.body.appendChild(a);
            a.click();

            // Clean up by removing the element and revoking the URL
            setTimeout(() => { // Delay cleanup to handle Firefox's slower download initiation
                window.URL.revokeObjectURL(url);
                document.body.removeChild(a);
            }, 100);
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
