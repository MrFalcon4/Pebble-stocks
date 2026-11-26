// EMULATOR TEST FIXTURE. Serves fixed, made-up quotes on finance/stock so the
// Stocks Viewer UI can be exercised without a phone. Every name says "(sample)"
// and the viewer shows a SAMPLE badge when this plugin answers. Not market data.

var SAMPLE = [
  ['AAPL', 'Apple (sample)', '$231.48', '+$2.87', '+1.26%'],
  ['MSFT', 'Microsoft (sample)', '$418.06', '-$3.12', '-0.74%'],
  ['NVDA', 'NVIDIA (sample)', '$142.90', '+$5.61', '+4.09%'],
  ['^GSPC', 'S&P 500 (sample)', '$5,982.11', '-$12.40', '-0.21%'],
  ['SAP', 'SAP SE (sample)', '€241.35', '+€0.00', '+0.00%'],
  ['BRK-A', 'Berkshire (sample)', '$712,450.00', '+$1,830.00', '+0.26%'],
  ['7203.T', 'Toyota (sample)', '¥2,615.50', '-¥31.00', '-1.17%']
];

function reading(text) {
  return { shortText: { text: text }, longText: { text: text } };
}

Pebble.registerSourceHandler(function (request, respond) {
  respond.data({
    validUntilMs: Date.now() + 60 * 1000,
    instances: SAMPLE.map(function (row) {
      return {
        instanceId: row[0],
        properties: {
          ticker: { shortText: { text: row[0] } },
          name: { shortText: { text: row[1] } },
          price: { shortText: { text: row[2] }, longText: { text: row[2] + '  ' + row[4] } },
          day_change: reading(row[3]),
          day_change_percent: reading(row[4])
        }
      };
    })
  });
});
