#pragma once
#include <Arduino.h>
#include <WebServer.h>

extern bool enableOnlineFonts;

// External Google Font import (used when device is connected to internet/Tailscale)
static const char GOOGLE_FONT_IMPORT[] =
"@import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap');";

// Shared CSS styling for all web pages (cyber-dark theme and responsive cards)
static const char COMMON_CSS[] =
":root{--bg:#000000;--surface:rgba(15,23,36,0.72);--surface-c:rgba(26,38,56,0.65);"
"--surface-border:rgba(255,255,255,0.08);--primary:#8ab4f8;--primary-glow:rgba(138,180,248,0.25);"
"--on-surface:#e2e8f0;--on-surface-v:#7c8ba1;--outline:rgba(255,255,255,0.07);"
"--error:#f87171;--warning:#fbbf24;--success:#34d399;}"
"*{box-sizing:border-box;margin:0;padding:0;}"
"html{height:100%;-webkit-text-size-adjust:100%;}"
"body{background-color:#000;background-image:radial-gradient(circle at 50% 0%,rgba(24,38,64,0.45) 0%,rgba(8,13,22,0.85) 65%,#000 100%);"
"background-attachment:fixed;color:var(--on-surface);min-height:100%;min-height:100dvh;"
"font-family:'Inter',-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"display:flex;justify-content:center;align-items:flex-start;margin:0;padding:0;-webkit-tap-highlight-color:transparent;overflow-x:hidden;}"
".wrap{width:100%;max-width:680px;padding:32px 20px 56px;min-height:100%;}"
".wrap.centered{max-width:440px;width:100%;min-height:100dvh;padding:20px;"
"display:flex;flex-direction:column;justify-content:center;align-items:stretch;align-self:center;box-sizing:border-box;}"
"@media (min-width:768px){.wrap{max-width:760px;padding:48px 32px 64px;}.wrap.centered{max-width:460px;padding:32px;}}"
"h1,h2{font-family:'Inter',sans-serif;font-weight:700;color:#f1f5f9;letter-spacing:-0.3px;margin:0 0 20px;text-align:center;}"
"h1{font-size:24px;}"
"h2{font-size:20px;}"
"h3{font-family:'Inter',sans-serif;font-size:11px;font-weight:600;color:var(--primary);text-transform:uppercase;"
"letter-spacing:1.5px;margin:28px 0 10px;padding:0;}"
".nav a{display:flex;align-items:center;justify-content:center;gap:8px;background:var(--surface-c);"
"backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);border:1px solid var(--surface-border);"
"color:var(--on-surface);font-family:'Inter',sans-serif;font-weight:600;font-size:14px;letter-spacing:0.3px;"
"text-decoration:none;padding:0 20px;margin:10px 0;border-radius:20px;height:52px;min-height:52px;line-height:1;"
"box-sizing:border-box;transition:all .2s cubic-bezier(0.4,0,0.2,1);}"
".nav a:hover{transform:translateY(-2px);background:rgba(35,52,78,0.75);border-color:rgba(138,180,248,0.4);"
"filter:brightness(1.12);box-shadow:0 8px 24px rgba(0,0,0,0.4),0 0 16px rgba(138,180,248,0.15);}"
".nav a:active{transform:translateY(1px) scale(0.98);filter:brightness(0.95);}"
".nav a.primary{background:linear-gradient(135deg,rgba(66,133,244,0.25) 0%,rgba(29,78,216,0.15) 100%);"
"border:1px solid rgba(138,180,248,0.35);color:var(--primary);box-shadow:0 4px 20px rgba(59,130,246,0.2);}"
".nav a.primary:hover{border-color:rgba(138,180,248,0.55);box-shadow:0 8px 26px rgba(59,130,246,0.38);transform:translateY(-2px);}"
".mono{font-family:'SF Mono',Menlo,Consolas,monospace;}"
".card{background:var(--surface);backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px);"
"border:1px solid var(--surface-border);box-shadow:0 10px 30px rgba(0,0,0,0.45);border-radius:20px;"
"padding:0;margin-bottom:16px;transition:transform .25s cubic-bezier(0.4,0,0.2,1),border-color .25s ease,box-shadow .25s ease;}"
".card:hover{transform:translateY(-3px);border-color:rgba(138,180,248,0.28);box-shadow:0 14px 40px rgba(0,0,0,0.55),0 0 20px rgba(138,180,248,0.08);}"
".row{display:flex;justify-content:space-between;align-items:center;padding:13px 20px;font-size:13.5px;"
"border-bottom:1px solid rgba(255,255,255,0.06);gap:16px;margin:0;position:relative;"
"transition:all .2s cubic-bezier(0.4,0,0.2,1);}"
".row:first-child{border-top-left-radius:19px;border-top-right-radius:19px;}"
".row:last-child{border-bottom:none;border-bottom-left-radius:19px;border-bottom-right-radius:19px;}"
".row:only-child{border-radius:19px;}"
".row:hover{background:rgba(138,180,248,0.08);transform:translateY(-1px);z-index:2;}"
".row .k{color:var(--on-surface-v);font-weight:500;flex-shrink:0;}"
".row .v{color:var(--on-surface);font-weight:600;text-align:right;word-break:normal;overflow-wrap:break-word;}"
".v.ok{color:var(--success);}.v.warn{color:var(--warning);}.v.danger{color:var(--error);}"
".log-item{display:flex;justify-content:space-between;align-items:center;padding:13px 20px;"
"border-bottom:1px solid rgba(255,255,255,0.06);gap:12px;margin:0;position:relative;"
"transition:all .2s cubic-bezier(0.4,0,0.2,1);}"
".log-item:first-child{border-top-left-radius:19px;border-top-right-radius:19px;}"
".log-item:last-child{border-bottom:none;border-bottom-left-radius:19px;border-bottom-right-radius:19px;}"
".log-item:only-child{border-radius:19px;}"
".log-item:hover{background:rgba(138,180,248,0.08);transform:translateY(-1px);z-index:2;}"
".log-meta{display:flex;flex-direction:column;gap:3px;flex:1;min-width:0;}"
".log-title{font-weight:600;font-size:13px;color:var(--on-surface);line-height:1.3;word-break:normal;overflow-wrap:break-word;}"
".log-title.danger{color:var(--error);}.log-title.warn{color:var(--warning);}.log-title.ok{color:var(--success);}"
".log-sub{font-family:'SF Mono',Menlo,Consolas,monospace;font-size:11px;color:var(--on-surface-v);}"
".log-badge{font-family:'SF Mono',Menlo,Consolas,monospace;font-size:10.5px;font-weight:600;padding:3px 8px;"
"border-radius:6px;background:rgba(255,255,255,0.06);color:var(--on-surface);text-transform:uppercase;letter-spacing:0.5px;flex-shrink:0;transition:all .2s ease;}"
".log-badge:hover{filter:brightness(1.15);}"
".row,.log-item{cursor:pointer;overflow:hidden;}"
".log-item.latest-entry{background:rgba(138,180,248,0.04);}"
".log-item.latest-entry::before{content:'';position:absolute;left:12px;top:50%;transform:translateY(-50%);height:60%;width:3px;background:var(--primary);border-radius:2px;}"
".log-item.latest-entry:hover{background:rgba(138,180,248,0.12);transform:translateY(-1px);z-index:2;}"
".copy-toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%) translateY(20px);background:rgba(26,38,56,0.95);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);border:1px solid rgba(138,180,248,0.3);color:var(--primary);font-size:12px;font-weight:600;padding:8px 18px;border-radius:20px;box-shadow:0 8px 24px rgba(0,0,0,0.6);opacity:0;pointer-events:none;transition:all .25s cubic-bezier(0.4,0,0.2,1);z-index:9999;}"
".copy-toast.show{opacity:1;transform:translateX(-50%) translateY(0);}"
".btn-log-toggle{width:100% !important;height:38px !important;min-height:38px !important;background:transparent !important;backdrop-filter:none !important;-webkit-backdrop-filter:none !important;border:none !important;border-top:1px solid rgba(255,255,255,0.06) !important;color:var(--primary) !important;font-family:'Inter',sans-serif;font-size:11.5px !important;font-weight:600 !important;letter-spacing:0.4px !important;text-transform:none !important;padding:0 16px !important;margin:0 !important;border-radius:0 0 19px 19px !important;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:6px;transition:background .2s ease,color .2s ease;box-shadow:none !important;transform:none !important;}"
".btn-log-toggle:hover{background:rgba(138,180,248,0.08) !important;transform:none !important;filter:none !important;box-shadow:none !important;}"
".btn-log-toggle:active{background:rgba(138,180,248,0.15) !important;transform:none !important;}"
".log-expandable{display:grid;grid-template-rows:0fr;transition:grid-template-rows .35s cubic-bezier(0.4,0,0.2,1);overflow:hidden;}"
".log-expandable.expanded{grid-template-rows:1fr;}"
".log-expandable-inner{min-height:0;overflow:hidden;}"
".log-arrow{display:inline-block;transition:transform .3s cubic-bezier(0.4,0,0.2,1);font-size:10px;margin-left:4px;}"
".log-badge.latest{background:rgba(138,180,248,0.18);border:1px solid rgba(138,180,248,0.35);color:var(--primary);}"
".log-badge.on{background:rgba(52,211,153,0.15);border:1px solid rgba(52,211,153,0.3);color:var(--success);}"
".log-badge.standby{background:rgba(138,180,248,0.15);border:1px solid rgba(138,180,248,0.3);color:var(--primary);}"
".actions{display:flex;gap:12px;margin-top:10px;}"
".actions form{flex:1;margin:0;}"
"input[type=password]{height:48px;min-height:48px;background:var(--surface-c);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);border:1px solid var(--surface-border);color:var(--on-surface);font-family:'Inter',sans-serif;font-size:13px;padding:0 16px;border-radius:24px;box-sizing:border-box;outline:none;text-align:center;transition:all .2s cubic-bezier(0.4,0,0.2,1);}"
"input[type=password]:hover{transform:translateY(-2px);border-color:rgba(138,180,248,0.4);filter:brightness(1.12);box-shadow:0 8px 24px rgba(0,0,0,0.4),0 0 16px rgba(138,180,248,0.15);}"
"input[type=password]:focus{border-color:var(--primary);box-shadow:0 0 16px rgba(138,180,248,0.25);transform:translateY(-2px);}"
"input[type=password]:active{transform:translateY(1px) scale(0.98);filter:brightness(0.95);}"
"button{width:100%;display:inline-flex;align-items:center;justify-content:center;gap:8px;background:var(--surface-c);"
"backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);border:1px solid var(--surface-border);color:var(--on-surface);"
"font-family:'Inter',sans-serif;font-weight:600;letter-spacing:0.4px;text-transform:uppercase;padding:0 18px;border-radius:24px;"
"cursor:pointer;font-size:12px;height:48px;min-height:48px;line-height:1;box-sizing:border-box;appearance:none;-webkit-appearance:none;"
"transition:all .2s cubic-bezier(0.4,0,0.2,1);}"
"button:hover{transform:translateY(-2px);border-color:rgba(255,255,255,0.2);filter:brightness(1.15);box-shadow:0 8px 24px rgba(0,0,0,0.4);}"
"button:active{transform:translateY(1px) scale(0.98);filter:brightness(0.95);}"
"button.danger{background:rgba(248,113,113,0.12);border-color:rgba(248,113,113,0.25);color:var(--error);}"
"button.danger:hover{background:rgba(248,113,113,0.22);border-color:rgba(248,113,113,0.5);box-shadow:0 8px 24px rgba(248,113,113,0.3);transform:translateY(-2px);}"
"button.warn{background:rgba(251,191,36,0.12);border-color:rgba(251,191,36,0.25);color:var(--warning);}"
"button.warn:hover{background:rgba(251,191,36,0.22);border-color:rgba(251,191,36,0.5);box-shadow:0 8px 24px rgba(251,191,36,0.3);transform:translateY(-2px);}"
".back{display:flex;align-items:center;justify-content:center;margin:20px auto 0;color:var(--on-surface-v);text-decoration:none;"
"font-size:13px;letter-spacing:0.3px;padding:10px 18px;border-radius:16px;transition:all .2s cubic-bezier(0.4,0,0.2,1);width:fit-content;}"
".back:hover{color:var(--primary);background:rgba(138,180,248,0.08);transform:translateY(-1px);}"
".back:active{transform:translateY(1px) scale(0.96);filter:brightness(0.9);background:rgba(138,180,248,0.18);}"
".pill{display:inline-block;padding:4px 12px;border-radius:20px;font-size:11px;font-weight:600;"
"letter-spacing:0.5px;text-transform:uppercase;transition:all .2s ease;}"
".pill:hover{filter:brightness(1.15);}"
".pill.on{background:rgba(52,211,153,0.15);border:1px solid rgba(52,211,153,0.3);color:var(--success);}"
".pill.off{background:rgba(124,139,161,0.12);border:1px solid rgba(124,139,161,0.25);color:var(--on-surface-v);}"
".pill.standby{background:rgba(138,180,248,0.15);border:1px solid rgba(138,180,248,0.3);color:var(--primary);}"
".divider{border:none;border-top:1px solid var(--outline);margin:24px 0;}";

// Background polling script (now bundled directly into cached /app.js for 0-byte inline overhead)
static const char POLL_SCRIPT[] = "";

// Shared client JavaScript for live telemetry polling, copy-to-clipboard actions, haptics, and log expansion
static const char APP_JS[] =
"var __poll;"
"function __fmtAgo(s){"
"if(s<=2)return 'Just now';"
"var d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),m=Math.floor((s%3600)/60),sec=s%60,o='';"
"if(d>0)o+=d+'d ';"
"if(h>0)o+=h+'h ';"
"if(m>0)o+=m+'m ';"
"if(sec>0||!o)o+=sec+'s';"
"return o.trim()+' ago';"
"}"
"function __fmtDur(s){"
"if(s<=0)return '0s';"
"var d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),m=Math.floor((s%3600)/60),sec=s%60,o='';"
"if(d>0)o+=d+'d ';"
"if(h>0)o+=h+'h ';"
"if(m>0)o+=m+'m ';"
"if(sec>0||!o)o+=sec+'s';"
"return o.trim();"
"}"
"function __pollTick(){"
"fetch('/api/live').then(function(r){return r.json();}).then(function(d){"
"var el;"
"if(el=document.getElementById('up'))el.textContent=d.u;"
"if(el=document.getElementById('upf'))el.textContent=d.uf;"
"if(el=document.getElementById('tmp'))el.innerHTML=(d.t>-50?(d.t+' &deg;C'):'-');"
"if(el=document.getElementById('pwr'))el.textContent='~'+d.p+' W';"
"if(el=document.getElementById('ram'))el.textContent=d.ru+'/'+d.rt+' KB';"
"if(el=document.getElementById('clk'))el.textContent=d.c+' MHz';"
"if(el=document.getElementById('servo-ago')){"
"if(d.sl>0&&d.st>=d.sl){el.textContent=__fmtAgo(d.st-d.sl);}"
"}"
"if(el=document.getElementById('ts-dur-val')){"
"if(d.ts>0&&d.st>=d.ts){el.textContent=__fmtDur(d.st-d.ts);}"
"}"
"var tsItem=document.getElementById('ts-item-0');"
"if(tsItem&&d.ts0_dur){"
"var b=document.getElementById('ts-badge-0'),t=document.getElementById('ts-title-0'),e=document.getElementById('ts-end-0'),l=document.getElementById('ts-dur-line-0'),dt=document.getElementById('ts-dt-0');"
"if(d.ts0_act===1){"
"if(b){b.className='log-badge on';b.textContent='ACTIVE';}"
"if(t)t.textContent='Active Session';"
"if(e)e.style.display='none';"
"if(l)l.innerHTML='Duration: <span id=\"ts-dur-val\">'+(d.ts>0&&d.st>=d.ts?__fmtDur(d.st-d.ts):d.ts0_dur)+'</span><span id=\"ts-dt-0\">'+(d.ts0_dt||'')+'</span>';"
"}else{"
"if(b){b.className='log-badge latest';b.textContent='LATEST';}"
"if(t)t.textContent='Tailscale Session';"
"if(e){e.style.display='';e.textContent='Ended: '+d.ts0_end;}"
"if(l)l.innerHTML='Duration: <b>'+d.ts0_dur+'</b><span id=\"ts-dt-0\">'+(d.ts0_dt||'')+'</span>';"
"}"
"}"
"if(el=document.getElementById('ts-pill')){"
"el.textContent=d.ts_st;el.className='pill '+d.ts_cls;"
"}"
"if(el=document.getElementById('ts-ip'))el.textContent=d.ts_ip;"
"var ipR=document.getElementById('ts-ip-row');"
"if(ipR){"
"if(d.ts_ip&&d.ts_ip!=='-'&&d.ts_ip!=='Not Valid'){ipR.style.display='';}"
"else{ipR.style.display='none';}"
"}"
"if(el=document.getElementById('ts-conn'))el.textContent=d.ts_conn;"
"var r=document.getElementById('ts-connect-row');"
"if(r){"
"if(d.ts_cls==='on'&&d.ts_cs){r.style.display='';if(el=document.getElementById('ts-conn-time'))el.textContent=d.ts_cs;}"
"else{r.style.display='none';}"
"}"
"}).catch(function(){}).finally(function(){__scheduleNext(3500);});"
"}"
"function __scheduleNext(ms){if(!document.hidden){clearTimeout(__poll);__poll=setTimeout(__pollTick,ms||3500);}}"
"function __pollStart(){if(!document.getElementById('up')&&!document.getElementById('ram'))return;if(__poll)return;__scheduleNext(3500);}"
"function __pollStop(){if(__poll){clearTimeout(__poll);__poll=null;}}"
"document.addEventListener('visibilitychange',function(){if(document.hidden)__pollStop();else __pollStart();});"
"function __showToast(msg){"
"var t=document.getElementById('copy-toast');"
"if(!t){t=document.createElement('div');t.id='copy-toast';t.className='copy-toast';document.body.appendChild(t);}"
"t.textContent=msg;t.classList.add('show');"
"clearTimeout(t._tm);t._tm=setTimeout(function(){t.classList.remove('show');},1600);"
"}"
"function __copyText(txt){"
"if(!txt)return;"
"var done=function(){"
"if(navigator.vibrate)navigator.vibrate(10);"
"__showToast('Copied to clipboard');"
"};"
"if(navigator.clipboard&&window.isSecureContext){"
"navigator.clipboard.writeText(txt).then(done).catch(function(){__copyFallback(txt,done);});"
"}else{__copyFallback(txt,done);}"
"}"
"function __copyFallback(txt,done){"
"var ta=document.createElement('textarea');"
"ta.value=txt;ta.style.cssText='position:fixed;top:0;left:0;opacity:0;pointer-events:none;';"
"document.body.appendChild(ta);ta.focus();ta.select();"
"try{document.execCommand('copy');done();}catch(e){}"
"document.body.removeChild(ta);"
"}"
"document.addEventListener('click',function(e){"
"var b=e.target.closest('button,.nav a,a.back');"
"if(b){"
"if(navigator.vibrate){"
"if(b.classList.contains('primary')||b.getAttribute('href')==='/'){"
"navigator.vibrate([35,15,45]);"
"}else if(b.classList.contains('back')){"
"navigator.vibrate(12);"
"}else if(b.classList.contains('danger')||b.classList.contains('warn')){"
"navigator.vibrate([20,35,20]);"
"}else{"
"navigator.vibrate(28);"
"}"
"}"
"return;"
"}"
"if(e.target.closest('input, select, textarea, [type=range], .no-copy')) return;"
"var item=e.target.closest('.row,.log-item');"
"if(item&&!item.classList.contains('no-copy')){"
"var vEl=item.querySelector('.v');"
"if(vEl){"
"__copyText(vEl.innerText.trim());"
"}else if(item.classList.contains('log-item')){"
"var tEl=item.querySelector('.log-title');"
"var sEls=item.querySelectorAll('.log-sub');"
"var sText=Array.from(sEls).map(function(s){return s.innerText.trim();}).join(' | ');"
"var txt=tEl?tEl.innerText.trim()+(sText?' ('+sText+')':''):item.innerText.trim();"
"__copyText(txt);"
"}"
"}"
"});"
"function __initLogToggles(){"
"document.querySelectorAll('.card').forEach(function(card){"
"var items=card.querySelectorAll('.log-item');"
"if(items.length>3&&!card.querySelector('.btn-log-toggle')){"
"var wrap=document.createElement('div');"
"wrap.className='log-expandable';"
"var inner=document.createElement('div');"
"inner.className='log-expandable-inner';"
"wrap.appendChild(inner);"
"items[2].style.borderBottom='none';"
"for(var i=3;i<items.length;i++){"
"inner.appendChild(items[i]);"
"}"
"items[items.length-1].style.borderBottom='none';"
"card.appendChild(wrap);"
"var moreCount=items.length-3;"
"var btn=document.createElement('button');"
"btn.type='button';"
"btn.className='btn-log-toggle no-copy';"
"btn.innerHTML='<span>Show More ('+moreCount+' more)</span><span class=\"log-arrow\">&#9662;</span>';"
"var exp=false;"
"btn.addEventListener('click',function(e){"
"e.stopPropagation();"
"exp=!exp;"
"wrap.classList.toggle('expanded',exp);"
"var lbl=btn.querySelector('span');"
"if(lbl)lbl.textContent=exp?'Show Less':('Show More ('+moreCount+' more)');"
"var arr=btn.querySelector('.log-arrow');"
"if(arr)arr.style.transform=exp?'rotate(180deg)':'rotate(0deg)';"
"if(navigator.vibrate)navigator.vibrate(8);"
"});"
"card.appendChild(btn);"
"}"
"});"
"}"
"function __initApp(){"
"__initLogToggles();"
"if(!document.hidden)__pollStart();"
"}"
"if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',__initApp);}"
"else{__initApp();}";

// Wraps an HTML body with standard page header linking to cached CSS and JS
inline String wrapPage(const char* title, const char* icon, const char* bodyHtml, const char* extraScript = "", bool centered = false) {
  String out;
  out.reserve(strlen(bodyHtml) + (extraScript ? strlen(extraScript) : 0) + 600);
  out += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1, viewport-fit=cover'><title>";
  out += title;
  out += "</title><link rel='icon' href='data:image/svg+xml,%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%3E%3Ctext y=\".9em\" font-size=\"90\"%3E";
  out += icon;
  out += "%3C/text%3E%3C/svg%3E'>";
  if (enableOnlineFonts) {
    out += "<link rel='stylesheet' href='https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap'>";
  }
  out += "<link rel='stylesheet' href='/style.css?v=3'><script defer src='/app.js?v=3'></script>";
  out += "</head><body><div class='wrap";
  if (centered) out += " centered";
  out += "'>";
  out += bodyHtml;
  out += "</div>";
  if (extraScript && extraScript[0]) out += extraScript;
  out += "</body></html>";
  return out;
}

// Streams web pages in unified chunks with zero heap allocation and minimal TCP packet fragmentation
template <typename F>
inline void sendWrappedPageStream(WebServer &server, const char* title, const char* icon, F bodyWriter, const char* extraScript = "", bool centered = false) {
  server.sendHeader("Connection", "close");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");

  char headBuf[600];
  snprintf(headBuf, sizeof(headBuf),
    "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1, viewport-fit=cover'><title>%s</title>"
    "<link rel='icon' href='data:image/svg+xml,%%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%%3E%%3Ctext y=\".9em\" font-size=\"90\"%%3E%s%%3C/text%%3E%%3C/svg%%3E'>"
    "%s"
    "<link rel='stylesheet' href='/style.css?v=3'><script defer src='/app.js?v=3'></script></head><body><div class='wrap%s'>",
    title, icon,
    enableOnlineFonts ? "<link rel='stylesheet' href='https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap'>" : "",
    centered ? " centered" : ""
  );
  server.sendContent(headBuf);

  bodyWriter();

  server.sendContent(F("</div>"));
  if (extraScript && extraScript[0]) {
    server.sendContent(extraScript);
  }
  server.sendContent(F("</body></html>"));
  server.sendContent(""); // Terminate chunked transfer
}

inline void sendWrappedPage(WebServer &server, const char* title, const char* icon, const char* bodyHtml, const char* extraScript = "", bool centered = false) {
  sendWrappedPageStream(server, title, icon, [&server, bodyHtml]() {
    server.sendContent(bodyHtml);
  }, extraScript, centered);
}
