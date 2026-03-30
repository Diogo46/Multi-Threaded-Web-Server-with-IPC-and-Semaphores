console.log('Test JavaScript file loaded successfully');

document.addEventListener('DOMContentLoaded', function() {
    console.log('DOM Content Loaded');
    const heading = document.querySelector('h1');
    if (heading) {
        heading.style.color = '#007bff';
    }
});
