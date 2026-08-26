let rssiChart;
let snrChart;

async function loadDashboard() {
    const [latestRes, historyRes, statsRes] = await Promise.all([
        fetch("/api/latest"),
        fetch("/api/history"),
        fetch("/api/stats")
    ]);

    const latest = await latestRes.json();
    const history = await historyRes.json();
    const stats = await statsRes.json();

    document.getElementById("totalPackets").textContent = stats.total_packets;
    document.getElementById("packetsLost").textContent = stats.packets_lost;
    document.getElementById("deliveryRate").textContent = stats.delivery_rate + "%";

    if (latest.packet_number !== undefined) {
        document.getElementById("latestRSSI").textContent =
            latest.gateway_rssi + " dBm";
        document.getElementById("latestSNR").textContent =
            latest.snr + " dB";
        document.getElementById("rssiGain").textContent =
            latest.rssi_gain === null ? "--" : "+" + latest.rssi_gain + " dB";
    }

    document.getElementById("status").textContent =
        stats.demo_mode ? "● DEMO MODE" : "● " + stats.gateway_status;

    updateCharts(history);
    updateTable(history);
}

function updateCharts(history) {
    const labels = history.map(x => x.packet_number);
    const direct = history.map(x =>
        x.packet_type === "DIRECT" ? x.gateway_rssi : null
    );
    const relay = history.map(x =>
        x.packet_type === "RELAY" ? x.gateway_rssi : null
    );
    const snr = history.map(x => x.snr);

    if (rssiChart) rssiChart.destroy();
    if (snrChart) snrChart.destroy();

    rssiChart = new Chart(document.getElementById("rssiChart"), {
        type: "line",
        data: {
            labels,
            datasets: [
                {label:"Direct RSSI", data:direct, spanGaps:true, tension:.25},
                {label:"Relay RSSI", data:relay, spanGaps:true, tension:.25}
            ]
        },
        options:{responsive:true,scales:{y:{title:{display:true,text:"RSSI (dBm)"}}}}
    });

    snrChart = new Chart(document.getElementById("snrChart"), {
        type:"line",
        data:{labels,datasets:[{label:"SNR",data:snr,tension:.25}]},
        options:{responsive:true,scales:{y:{title:{display:true,text:"SNR (dB)"}}}}
    });
}

function updateTable(history) {
    const filter = document.getElementById("filter").value;
    const rows = history.slice().reverse()
        .filter(x => filter === "ALL" || x.packet_type === filter)
        .slice(0, 15);

    document.getElementById("packetTable").innerHTML = rows.map(x => `
        <tr>
            <td>${x.packet_number}</td>
            <td>${x.packet_type}</td>
            <td>${x.gateway_rssi ?? "--"} dBm</td>
            <td>${x.snr ?? "--"} dB</td>
            <td>${x.relay_rssi ?? "--"} dBm</td>
            <td>${x.rssi_gain == null ? "--" : "+" + x.rssi_gain + " dB"}</td>
            <td>${x.timestamp}</td>
        </tr>
    `).join("");
}

document.getElementById("filter").addEventListener("change", loadDashboard);
loadDashboard();
setInterval(loadDashboard, 5000);
