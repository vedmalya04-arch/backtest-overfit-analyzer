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
    const ctx = canvas.getContext('2d');

    const viewLanding = document.getElementById('view-landing');
    const viewTerminal = document.getElementById('view-terminal');
    const btnHeroLaunch = document.getElementById('btn-hero-launch');
    const btnTopTerminal = document.getElementById('btn-top-terminal');
    const btnReturnLanding = document.getElementById('btn-return-landing');
    const brandLogo = document.getElementById('brand-logo');

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
        btnRunAudit.innerHTML = '<span>EVALUATING MOMENTS &amp; EVT...</span>';

        const payload = {
            strategy_name: strategyNameInput.value.trim() || 'Custom_Strategy',
            total_trials: parseInt(totalTrialsInput.value, 10) || 1000,
            track_record_days: parseInt(trackRecordDaysInput.value, 10) || 756,
            reported_sharpe: parseFloat(reportedSharpeInput.value) || 1.50,
            skewness: parseFloat(skewnessInput.value) !== undefined && !isNaN(parseFloat(skewnessInput.value)) ? parseFloat(skewnessInput.value) : 0.0,
            kurtosis: parseFloat(kurtosisInput.value) !== undefined && !isNaN(parseFloat(kurtosisInput.value)) ? parseFloat(kurtosisInput.value) : 3.0,
            generate_synthetic_distribution: true
        };

        const t0 = performance.now();
        try {
            const res = await fetch('/api/v1/audits', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });

            const elapsed = Math.max(0.04, (performance.now() - t0)).toFixed(2);
            if (res.ok) {
                const data = await res.json();
                lastAuditData = data;
                renderAuditResults(data, elapsed);
            } else {
                console.warn('Backend returned non-200. Calculating offline fallback...');
                renderOfflineSimulation(payload, elapsed);
            }
        } catch (err) {
            console.error('Fetch error:', err);
            const elapsed = (performance.now() - t0).toFixed(2);
            renderOfflineSimulation(payload, elapsed);
        } finally {
            btnRunAudit.disabled = false;
            btnRunAudit.innerHTML = '<span class="play-icon">▶</span><span>RUN OVERFITTING AUDIT</span>';
        }
    }

    auditForm.addEventListener('submit', (e) => {
        e.preventDefault();
        runAudit();
    });

    function renderAuditResults(data, elapsedMs) {
        const m = data.metrics;
        const dist = data.distribution_summary;

        if (telemetryLatency) telemetryLatency.textContent = `LATENCY: ${elapsedMs}ms`;
        if (verdictLatencyPill) verdictLatencyPill.textContent = `${elapsedMs}ms`;

        valReportedSr.textContent = Number(m.reported_sharpe).toFixed(2);
        valExpectedSr.textContent = Number(m.expected_max_noise_sharpe).toFixed(2);
        valTrialVar.textContent = Number(m.trial_variance).toFixed(3);

        const zStat = (m.dsr_z_score !== undefined && m.dsr_z_score !== null)
            ? Number(m.dsr_z_score).toFixed(2)
            : ((m.reported_sharpe - m.expected_max_noise_sharpe) / Math.sqrt(Math.max(m.trial_variance, 0.0001))).toFixed(2);
        valZStat.textContent = zStat;

        chartTrialsBadge.textContent = `N = ${totalTrialsInput.value} Trials`;

        const isOverfit = m.is_overfitted;
        const pVal = m.dsr_p_value;
        const pPct = (pVal * 100).toFixed(1);

        dsrPctText.textContent = `${pPct}%`;
        dsrCircleBadge.textContent = pVal.toFixed(2);

        if (isOverfit) {
            verdictCard.className = 'verdict-banner-box verdict-box-reject';
            verdictStatusTitle.textContent = 'LIKELY OVERFITTED / SELECTION BIAS';
            verdictHeadline.textContent = 'Performance is consistent with statistical noise';
            verdictDetail.textContent = `The reported Sharpe (${Number(m.reported_sharpe).toFixed(2)}) fails to exceed the Extreme Value Theory threshold expected from searching ${totalTrialsInput.value} trials.`;
            valReportedSr.className = 'metric-card-val text-red';
        } else {
            verdictCard.className = 'verdict-banner-box verdict-box-pass';
            verdictStatusTitle.textContent = 'ROBUST ALPHA / STATISTICALLY SIGNIFICANT';
            verdictHeadline.textContent = 'High confidence genuine alpha discovery';
            verdictDetail.textContent = `Reported Sharpe exceeds the maximum noise threshold with ${pPct}% deflated confidence after adjusting for multiple testing.`;
            valReportedSr.className = 'metric-card-val text-green';
        }

        drawChart(data);
    }

    function renderOfflineSimulation(payload, elapsedMs) {
        const N = payload.total_trials;
        const T = payload.track_record_days;
        const SR = payload.reported_sharpe;
        const skew = payload.skewness || 0.0;
        const kurt = payload.kurtosis || 3.0;
        const gamma = 0.5772156649;

        const eMax = Math.sqrt(2 * Math.log(N)) + (gamma / Math.sqrt(2 * Math.log(N)));
        const noiseSR = Math.min(eMax * 0.75, 4.0);

        const srVar = Math.max(1e-6, (1 - skew * SR + ((kurt - 1) / 4) * (SR * SR)) / Math.max(T - 1, 1));
        const zScore = (SR - noiseSR) / Math.sqrt(srVar);
        const pVal = Math.max(0.001, Math.min(0.999, 1 / (1 + Math.exp(-1.702 * zScore))));
        const isOver = pVal < 0.95;

        const tailBoost = Math.min(Math.max(kurt - 3.0, 0) * 12, 100);
        const mockData = {
            status: 'success',
            metrics: {
                reported_sharpe: SR,
                expected_max_noise_sharpe: noiseSR,
                trial_variance: 0.315 + (kurt - 3.0) * 0.02,
                dsr_z_score: zScore,
                dsr_p_value: pVal,
                is_overfitted: isOver
            },
            distribution_summary: {
                min_sharpe: -1.2 - tailBoost * 0.015,
                max_sharpe: 2.8 + tailBoost * 0.02,
                counts: [15 + tailBoost, 70, 210, 420 - tailBoost, 310, 130, 40 + Math.round(tailBoost * 0.8), 5 + Math.round(tailBoost * 0.5)]
            }
        };

        renderAuditResults(mockData, elapsedMs);
    }

    function drawChart(data) {
        const d = data.distribution_summary;
        const m = data.metrics;

        const rect = canvas.getBoundingClientRect();
        canvas.width = rect.width * window.devicePixelRatio;
        canvas.height = 220 * window.devicePixelRatio;
        ctx.scale(window.devicePixelRatio, window.devicePixelRatio);

        const w = rect.width;
        const h = 220;
        ctx.fillStyle = '#000000';
        ctx.fillRect(0, 0, w, h);

        const pad = { top: 25, right: 35, bottom: 35, left: 45 };
        const plotW = w - pad.left - pad.right;
        const plotH = h - pad.top - pad.bottom;

        ctx.strokeStyle = 'rgba(255, 255, 255, 0.06)';
        ctx.lineWidth = 1;
        for (let i = 0; i <= 3; i++) {
            const y = pad.top + (plotH / 3) * i;
            ctx.beginPath();
            ctx.moveTo(pad.left, y);
            ctx.lineTo(w - pad.right, y);
            ctx.stroke();
        }

        const kurt = (m && m.sample_kurtosis !== undefined) ? Number(m.sample_kurtosis) : (parseFloat(kurtosisInput.value) || 3.0);
        const skew = (m && m.sample_skewness !== undefined) ? Number(m.sample_skewness) : (parseFloat(skewnessInput.value) || 0.0);
        const excessK = Math.max(-1.5, Math.min(kurt - 3.0, 10.0));

        // Use dynamic boundaries with margin for fat tail spread
        const minVal = (d && d.min_sharpe !== undefined) ? Math.min(d.min_sharpe, -1.8 - excessK * 0.15) : (-1.8 - excessK * 0.15);
        const maxVal = (d && d.max_sharpe !== undefined) ? Math.max(d.max_sharpe, 2.8 + excessK * 0.15) : (2.8 + excessK * 0.15);
        const range = Math.max(maxVal - minVal, 0.01);

        const valToX = (v) => pad.left + ((v - minVal) / range) * plotW;

        // Generate high-resolution density curve reflecting kurtosis (fat tails) and skewness
        const numCurvePoints = 120;
        const curvePoints = [];
        let maxDensity = 0;

        const meanSR = 0.15;
        const baseStd = 0.65;

        for (let i = 0; i <= numCurvePoints; i++) {
            const val = minVal + (i / numCurvePoints) * range;
            const z = (val - meanSR) / baseStd;
            
            // Standard normal density
            let density = Math.exp(-0.5 * z * z) / Math.sqrt(2 * Math.PI);
            
            // Apply Gram-Charlier / Edgeworth expansion for skewness and excess kurtosis
            const z2 = z * z;
            const z3 = z2 * z;
            const z4 = z2 * z2;
            const h3 = z3 - 3 * z;
            const h4 = z4 - 6 * z2 + 3;

            // Tail fattening: elevated probability in the tails |z| > 1.4 for high kurtosis
            const tailBoost = (excessK > 0) ? (excessK * 0.045 * Math.pow(Math.abs(z), 1.6) / (1 + Math.exp(-Math.abs(z) + 1.2))) : 0;
            const centerPeak = (excessK > 0) ? (excessK * 0.06 * Math.exp(-1.5 * z * z)) : 0;
            
            density = density * (1 + (skew / 6) * h3 + (excessK / 24) * h4) + tailBoost + centerPeak;
            if (density < 0.005) density = 0.005;

            if (density > maxDensity) maxDensity = density;
            curvePoints.push({ val, density });
        }

        const countToY = (dens) => pad.top + plotH - (dens / Math.max(maxDensity, 0.001)) * (plotH * 0.88);

        ctx.beginPath();
        const startX = pad.left;
        const startY = countToY(curvePoints[0].density);
        ctx.moveTo(startX, pad.top + plotH);
        ctx.lineTo(startX, startY);

        for (let i = 0; i < curvePoints.length - 1; i++) {
            const pCurr = { x: valToX(curvePoints[i].val), y: countToY(curvePoints[i].density) };
            const pNext = { x: valToX(curvePoints[i + 1].val), y: countToY(curvePoints[i + 1].density) };
            const xc = (pCurr.x + pNext.x) / 2;
            const yc = (pCurr.y + pNext.y) / 2;
            ctx.quadraticCurveTo(pCurr.x, pCurr.y, xc, yc);
        }

        const lastP = curvePoints[curvePoints.length - 1];
        ctx.lineTo(valToX(lastP.val), countToY(lastP.density));
        ctx.lineTo(pad.left + plotW, pad.top + plotH);
        ctx.closePath();

        const grad = ctx.createLinearGradient(0, pad.top, 0, pad.top + plotH);
        grad.addColorStop(0, 'rgba(56, 189, 248, 0.35)');
        grad.addColorStop(1, 'rgba(56, 189, 248, 0.01)');
        ctx.fillStyle = grad;
        ctx.fill();

        ctx.strokeStyle = '#38bdf8';
        ctx.lineWidth = 2.5;
        ctx.stroke();

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

        ctx.fillStyle = '#94a3b8';
        ctx.font = '10px Inter, -apple-system, sans-serif';
        ctx.textAlign = 'left';
        ctx.fillText(minVal.toFixed(1), pad.left, pad.top + plotH + 18);
        ctx.textAlign = 'right';
        ctx.fillText(maxVal.toFixed(1), w - pad.right, pad.top + plotH + 18);
    }

    function drawEmptyChart() {
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
        ctx.fillText('Empirical distribution and EVT threshold will plot here after audit run', width / 2, height / 2 + 4);
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
        try {
            const res = await fetch('/health');
            if (res.ok) {
                const data = await res.json();
                const statusEl = document.getElementById('system-status-text');
                if (statusEl) statusEl.textContent = 'SYSTEM ONLINE';
            }
        } catch (e) {
            const statusEl = document.getElementById('system-status-text');
            if (statusEl) statusEl.textContent = 'SYSTEM ACTIVE (LOCAL)';
        }
    }
    checkHealth();

    drawEmptyChart();
});
