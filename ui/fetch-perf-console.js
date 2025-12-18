const url = 'http://localhost:8080/state';
const runs = 10;
let count = 0;

const intervalId = setInterval(() => {
  const start = performance.now();

  fetch(url)
    .then(response => {
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      return response.arrayBuffer();
    })
    .then(buffer => {
      const duration = performance.now() - start;
      const sizeBytes = buffer.byteLength;

      count++;
      console.log(
        `Run ${count}: ${duration.toFixed(2)} ms, ${sizeBytes} bytes`
      );

      if (count >= runs) {
        clearInterval(intervalId);
        console.log('Done.');
      }
    })
    .catch(err => {
      clearInterval(intervalId);
      console.error('Request failed:', err);
    });

}, 1000);
