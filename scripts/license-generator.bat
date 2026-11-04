@echo off
rem AmF X1 license key generator: double-click to open it in your browser (this computer only). Needs Node.js 18+.
cd /d "%~dp0.."
where node >nul 2>nul || (echo Node.js is required: https://nodejs.org/ & pause & exit /b 1)
node scripts\license-generator.js --open %*
pause
