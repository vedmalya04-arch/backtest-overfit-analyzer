document.addEventListener('DOMContentLoaded', () => {
    const strategyNameInput = document.getElementById('strategy_name');
    const reportedSharpeInput = document.getElementById('reported_sharpe');
    const totalTrialsInput = document.getElementById('total_trials');
    const trackRecordDaysInput = document.getElementById('track_record_days');
    const skewnessInput = document.getElementById('skewness');
    const kurtosisInput = document.getElementById('kurtosis');
    const auditForm = document.getElementById('audit-form');
    const btnRunAudit = document.getElementById('btn-run-audit');

    const verdictCard = document.getElementById('verdict-card');
    const verdictStatusTitle = document.getElementById('verdict-status-title');
    const verdictHeadline = document.getElementById('verdict-headline');
    const verdictDetail = document.getElementById('verdict-detail');
    const verdictLatencyPill = document.getElementById('verdict-latency-pill');
    const dsrPctText = document.getElementById('dsr-pct-text');
    const dsrCircleBadge = document.getElementById('dsr-circle-badge');

    const valReportedSr = document.getElementById('val-reported-sr');
    const valExpectedSr = document.getElementById('val-expected-sr');
    const valTrialVar = document.getElementById('val-trial-var');
    const valZStat = document.getElementById('val-z-stat');
    const chartTrialsBadge = document.getElementById('chart-trials-badge');
    const telemetryLatency = document.getElementById('telemetry-latency');

    const canvas = document.getElementById('distribution-canvas');
    const ctx = canvas ? canvas.getContext('2d') : null;

    const viewLanding = document.getElementById('view-landing');
    const viewTerminal = document.getElementById('view-terminal');
    const btnHeroLaunch = document.getElementById('btn-hero-launch');
    const btnTopTerminal = document.getElementById('btn-top-terminal');
    const btnReturnLanding = document.getElementById('btn-return-landing');
    const brandLogo = document.getElementById('brand-logo');

    // Dynamic Backend URL Management
    function getBackendUrl() {
        const urlParams = new URLSearchParams(window.location.search);
        if (urlParams.get('api')) {
            const api = urlParams.get('api').replace(/\/+$/, '');
            localStorage.setItem('DSR_BACKEND_URL', api);
            return api;
        }

        const saved = localStorage.getItem('DSR_BACKEND_URL');
        if (saved && saved.trim().length > 0) {
            return saved.trim().replace(/\/+$/, '');
        }

        // If local development without running through C++ port 8088
        if (window.location.hostname === 'localhost' || window.location.hostname === '127.0.0.1') {
            if (window.location.port !== '8088') {
                return 'http://127.0.0.1:8088';
            }
            return '';
        }

        // If hosted on Render directly, backend is same origin
        if (window.location.hostname.includes('onrender.com')) {
            return '';
        }

        // Default to same-origin relative paths
        return '';
    }

    function promptBackendConfig() {
        const current = getBackendUrl() || (window.location.origin);
        const input = prompt(
            "Enter C++ DSR Engine Backend URL:\n(e.g., https://your-app.onrender.com or http://127.0.0.1:8088)",
            current
        );
        if (input !== null) {
            const cleaned = input.trim().replace(/\/+$/, '');
            if (cleaned.length > 0) {
                localStorage.setItem('DSR_BACKEND_URL', cleaned);
            } else {
                localStorage.removeItem('DSR_BACKEND_URL');
            }
            checkHealth();
        }
    }

    // Connect pill to configure backend URL
    const terminalPill = document.querySelector('.terminal-engine-active-pill');
    if (terminalPill) {
        terminalPill.style.cursor = 'pointer';
        terminalPill.title = 'Click to configure C++ Backend API endpoint';
        terminalPill.addEventListener('click', promptBackendConfig);
    }

    function showTerminal() {
        if (viewLanding && viewTerminal) {
            viewLanding.style.display = 'none';
            viewTerminal.style.display = 'block';
            window.scrollTo({ top: 0, behavior: 'smooth' });
            drawCurrentChart();
        }
    }

    function showLanding() {
        if (viewLanding && viewTerminal) {
            viewTerminal.style.display = 'none';
            viewLanding.style.display = 'block';
        }
    }

    if (btnHeroLaunch) btnHeroLaunch.addEventListener('click', showTerminal);
    if (btnTopTerminal) btnTopTerminal.addEventListener('click', showTerminal);
    if (btnReturnLanding) btnReturnLanding.addEventListener('click', showLanding);
    if (brandLogo) brandLogo.addEventListener('click', (e) => {
        e.preventDefault();
        showLanding();
        window.scrollTo({ top: 0, behavior: 'smooth' });
    });

    let lastAuditData = null;

    async function runAudit() {
        btnRunAudit.disabled = true;
        btnRunAudit.innerHTML = '<span>EVALUATING IN C++ ENGINE...</span>';

        const rawSr = reportedSharpeInput ? reportedSharpeInput.value.trim() : '';
        const parsedSr = parseFloat(rawSr);
        const reportedSr = !isNaN(parsedSr) ? parsedSr : 1.0;

        const rawTrials = totalTrialsInput ? totalTrialsInput.value.trim() : '';
        const parsedTrials = parseInt(rawTrials, 10);
        const trials = (!isNaN(parsedTrials) && parsedTrials >= 2) ? parsedTrials : 10;

        const rawDays = trackRecordDaysInput ? trackRecordDaysInput.value.trim() : '';
        const parsedDays = parseInt(rawDays, 10);
        const days = (!isNaN(parsedDays) && parsedDays >= 10) ? parsedDays : 365;

        const payload = {
            strategy_name: (strategyNameInput && strategyNameInput.value.trim()) ? strategyNameInput.value.trim() : 'Strategy_Custom',
            total_trials: trials,
            track_record_days: days,
            reported_sharpe: reportedSr,
            skewness: (skewnessInput && skewnessInput.value !== '' && !isNaN(parseFloat(skewnessInput.value))) ? parseFloat(skewnessInput.value) : 0.0,
            kurtosis: (kurtosisInput && kurtosisInput.value !== '' && !isNaN(parseFloat(kurtosisInput.value))) ? parseFloat(kurtosisInput.value) : 3.0,
            generate_synthetic_distribution: true
        };

        const t0 = performance.now();
        const base = getBackendUrl();
        const targetUrl = `${base}/api/v1/audits`;

        try {
            const res = await fetch(targetUrl, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });

            const elapsed = Math.max(0.04, (performance.now() - t0)).toFixed(2);
            if (!res.ok) {
                const errText = await res.text();
                throw new Error(`Server returned HTTP ${res.status}: ${errText}`);
            }

            const data = await res.json();
            lastAuditData = data;
            renderAuditResults(data, elapsed);

        } catch (err) {
            console.error('Audit execution error:', err);
            const elapsed = (performance.now() - t0).toFixed(2);
            renderError(err.message, targetUrl, elapsed);
        } finally {
            btnRunAudit.disabled = false;
            btnRunAudit.innerHTML = '<span class="play-icon">▶</span><span>RUN OVERFITTING AUDIT</span>';
        }
    }

    auditForm.addEventListener('submit', (e) => {
        e.preventDefault();
        runAudit();
    });

    function renderError(errMsg, targetUrl, elapsedMs) {
        if (telemetryLatency) telemetryLatency.textContent = `LATENCY: ${elapsedMs}ms (ERROR)`;
        if (verdictLatencyPill) verdictLatencyPill.textContent = `${elapsedMs}ms`;

        verdictCard.className = 'verdict-banner-box verdict-box-reject';
        verdictStatusTitle.textContent = 'BACKEND CONNECTION ERROR';
        verdictHeadline.textContent = 'Could not reach C++ DSR Engine';
        verdictDetail.innerHTML = `Attempted endpoint: <code>${targetUrl}</code><br>Reason: ${errMsg}<br><a href="#" id="link-fix-endpoint" style="color: #38bdf8; text-decoration: underline; margin-top: 8px; display: inline-block;">Click here to set your Render Backend URL</a>`;

        const linkFix = document.getElementById('link-fix-endpoint');
        if (linkFix) {
            linkFix.addEventListener('click', (e) => {
                e.preventDefault();
                promptBackendConfig();
            });
        }
    }

    function renderAuditResults(data, elapsedMs) {
        const m = data.metrics;

        if (telemetryLatency) telemetryLatency.textContent = `LATENCY: ${elapsedMs}ms`;
        if (verdictLatencyPill) verdictLatencyPill.textContent = `${elapsedMs}ms`;

        valReportedSr.textContent = Number(m.reported_sharpe).toFixed(2);
        valExpectedSr.textContent = Number(m.expected_max_noise_sharpe).toFixed(2);
        valTrialVar.textContent = Number(m.trial_variance).toFixed(3);

        const zStat = (m.dsr_z_score !== undefined && m.dsr_z_score !== null)
            ? Number(m.dsr_z_score).toFixed(2)
            : '0.00';
        valZStat.textContent = zStat;

        chartTrialsBadge.textContent = `N = ${m.total_trials || totalTrialsInput.value} Trials`;

        const isOverfit = m.is_overfitted;
        const pVal = m.dsr_p_value;
        const pPct = (pVal * 100).toFixed(1);

        dsrPctText.textContent = `${pPct}%`;
        dsrCircleBadge.textContent = pVal.toFixed(2);

        if (isOverfit) {
            verdictCard.className = 'verdict-banner-box verdict-box-reject';
            verdictStatusTitle.textContent = 'LIKELY OVERFITTED / SELECTION BIAS';
            verdictHeadline.textContent = 'Performance is consistent with statistical noise';
            verdictDetail.textContent = `The reported Sharpe (${Number(m.reported_sharpe).toFixed(2)}) fails to exceed the Extreme Value Theory threshold expected from searching ${m.total_trials || totalTrialsInput.value} trials (E[max]=${Number(m.expected_max_noise_sharpe).toFixed(2)}).`;
            valReportedSr.className = 'metric-card-val text-red';
        } else {
            verdictCard.className = 'verdict-banner-box verdict-box-pass';
            verdictStatusTitle.textContent = 'ROBUST ALPHA / STATISTICALLY SIGNIFICANT';
            verdictHeadline.textContent = 'High confidence genuine alpha discovery';
            verdictDetail.textContent = `Reported Sharpe (${Number(m.reported_sharpe).toFixed(2)}) exceeds the maximum noise threshold (${Number(m.expected_max_noise_sharpe).toFixed(2)}) with ${pPct}% deflated confidence after adjusting for ${m.total_trials || totalTrialsInput.value} trials.`;
            valReportedSr.className = 'metric-card-val text-green';
        }

        drawChart(data);
    }

    function drawChart(data) {
        if (!canvas || !ctx) return;
        const d = data.distribution_summary;
        const m = data.metrics;

        const rect = canvas.getBoundingClientRect();
        const w = rect.width || 600;
        const h = 220;
        canvas.width = w * window.devicePixelRatio;
        canvas.height = h * window.devicePixelRatio;
        ctx.scale(window.devicePixelRatio, window.devicePixelRatio);

        ctx.fillStyle = '#000000';
        ctx.fillRect(0, 0, w, h);

        const pad = { top: 25, right: 35, bottom: 35, left: 45 };
        const plotW = w - pad.left - pad.right;
        const plotH = h - pad.top - pad.bottom;

        // Grid lines
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
        ctx.lineWidth = 1;
        for (let i = 0; i <= 3; i++) {
            const y = pad.top + (plotH / 3) * i;
            ctx.beginPath();
            ctx.moveTo(pad.left, y);
            ctx.lineTo(w - pad.right, y);
            ctx.stroke();
        }

        const counts = (d && d.sharpe_histogram && d.sharpe_histogram.length > 0) ? d.sharpe_histogram : [];
        const maxC = counts.length > 0 ? Math.max(...counts, 1) : 1;
        
        // Dynamic horizontal boundaries covering data range, E[max], and candidate SR*
        let minVal = (d && d.min_sharpe !== undefined) ? d.min_sharpe : -1.5;
        let maxVal = (d && d.max_sharpe !== undefined) ? d.max_sharpe : 2.5;
        
        minVal = Math.min(minVal, m.reported_sharpe - 0.5, m.expected_max_noise_sharpe - 0.5);
        maxVal = Math.max(maxVal, m.reported_sharpe + 0.5, m.expected_max_noise_sharpe + 0.5);
        const range = Math.max(maxVal - minVal, 0.01);

        const valToX = (v) => pad.left + ((v - minVal) / range) * plotW;
        const countToY = (c) => pad.top + plotH - (c / maxC) * (plotH * 0.85);

        // Draw histogram bars from C++ bins
        const numBins = counts.length;
        const binStep = plotW / numBins;
        const barPad = 3;

        ctx.fillStyle = 'rgba(56, 189, 248, 0.15)';
        ctx.strokeStyle = 'rgba(56, 189, 248, 0.4)';
        ctx.lineWidth = 1;

        for (let i = 0; i < numBins; i++) {
            const bx = pad.left + i * binStep + barPad;
            const bw = Math.max(1, binStep - barPad * 2);
            const by = countToY(counts[i]);
            const bh = (pad.top + plotH) - by;

            ctx.fillRect(bx, by, bw, bh);
            ctx.strokeRect(bx, by, bw, bh);
        }

        // Draw smooth density spline across bin midpoints
        if (numBins >= 2) {
            ctx.beginPath();
            const firstX = pad.left + binStep * 0.5;
            const firstY = countToY(counts[0]);
            ctx.moveTo(firstX, firstY);

            for (let i = 0; i < numBins - 1; i++) {
                const x1 = pad.left + (i + 0.5) * binStep;
                const y1 = countToY(counts[i]);
                const x2 = pad.left + (i + 1.5) * binStep;
                const y2 = countToY(counts[i + 1]);
                const xc = (x1 + x2) / 2;
                const yc = (y1 + y2) / 2;
                ctx.quadraticCurveTo(x1, y1, xc, yc);
            }
            const lastX = pad.left + (numBins - 0.5) * binStep;
            const lastY = countToY(counts[numBins - 1]);
            ctx.lineTo(lastX, lastY);

            ctx.strokeStyle = '#38bdf8';
            ctx.lineWidth = 2.5;
            ctx.stroke();
        }

        // EVT E[max] Threshold Line
        const evtX = valToX(m.expected_max_noise_sharpe);
        if (evtX >= pad.left && evtX <= w - pad.right) {
            ctx.strokeStyle = '#f59e0b';
            ctx.lineWidth = 2;
            ctx.setLineDash([4, 4]);
            ctx.beginPath();
            ctx.moveTo(evtX, pad.top - 5);
            ctx.lineTo(evtX, pad.top + plotH);
            ctx.stroke();
            ctx.setLineDash([]);

            ctx.fillStyle = '#fbbf24';
            ctx.font = '600 11px Inter, -apple-system, sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText(`EVT E[max]=${Number(m.expected_max_noise_sharpe).toFixed(2)}`, evtX, pad.top - 10);
        }

        // Candidate Strategy Sharpe Line (SR*)
        const srX = valToX(m.reported_sharpe);
        if (srX >= pad.left && srX <= w - pad.right) {
            ctx.strokeStyle = '#34d399';
            ctx.lineWidth = 2.5;
            ctx.beginPath();
            ctx.moveTo(srX, pad.top - 5);
            ctx.lineTo(srX, pad.top + plotH);
            ctx.stroke();

            ctx.fillStyle = '#34d399';
            ctx.font = '700 11px Inter, -apple-system, sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText(`SR*=${Number(m.reported_sharpe).toFixed(2)}`, srX, pad.top + plotH + 22);
        }

        // Axis labels
        ctx.fillStyle = '#94a3b8';
        ctx.font = '10px Inter, -apple-system, sans-serif';
        ctx.textAlign = 'left';
        ctx.fillText(minVal.toFixed(1), pad.left, pad.top + plotH + 18);
        ctx.textAlign = 'right';
        ctx.fillText(maxVal.toFixed(1), w - pad.right, pad.top + plotH + 18);
    }

    function drawEmptyChart() {
        if (!canvas || !ctx) return;
        const rect = canvas.getBoundingClientRect();
        const width = rect.width || 600;
        const height = 220;
        canvas.width = width * window.devicePixelRatio;
        canvas.height = height * window.devicePixelRatio;
        ctx.scale(window.devicePixelRatio, window.devicePixelRatio);

        ctx.fillStyle = '#000000';
        ctx.fillRect(0, 0, width, height);

        const pad = { top: 25, right: 35, bottom: 35, left: 45 };
        const plotW = width - pad.left - pad.right;
        const plotH = height - pad.top - pad.bottom;

        ctx.strokeStyle = 'rgba(255, 255, 255, 0.04)';
        ctx.lineWidth = 1;
        for (let i = 0; i <= 3; i++) {
            const y = pad.top + (plotH / 3) * i;
            ctx.beginPath();
            ctx.moveTo(pad.left, y);
            ctx.lineTo(width - pad.right, y);
            ctx.stroke();
        }

        ctx.fillStyle = '#64748b';
        ctx.font = '500 12px Inter, -apple-system, sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText('Real empirical distribution from C++ Monte Carlo trials will plot here', width / 2, height / 2 + 4);
    }

    function drawCurrentChart() {
        if (lastAuditData) {
            drawChart(lastAuditData);
        } else {
            drawEmptyChart();
        }
    }

    window.addEventListener('resize', drawCurrentChart);

    async function checkHealth() {
        const base = getBackendUrl();
        try {
            const res = await fetch(`${base}/health`);
            if (res.ok) {
                const data = await res.json();
                if (terminalPill) {
                    terminalPill.innerHTML = `<span class="badge-pulse" style="background:#22c55e;"></span><span>C++ ENGINE ONLINE (${data.version || 'v1.0'})</span>`;
                }
            } else {
                throw new Error("Non-200");
            }
        } catch (e) {
            if (terminalPill) {
                terminalPill.innerHTML = `<span class="badge-pulse" style="background:#ef4444;"></span><span>C++ BACKEND OFFLINE (CLICK TO SET URL)</span>`;
            }
        }
    }

    checkHealth();
    drawEmptyChart();
});
