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
    console.log("value: " + display.value)
    // todo: implement it
    if (display.value == "114514") {
        display.style.borderColor = 'green';
    } else {
        display.style.borderColor = 'red';
        setTimeout(function () {
            display.style.borderColor = '';
        }, 500); // Reset after 500ms
    }
}
