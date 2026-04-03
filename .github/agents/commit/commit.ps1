# PowerShell script for /commit skill
# Stages changes, generates smart commit message, and pushes to git

param(
    [string]$ProjectPath = $PWD,
    [bool]$Push = $false
)

Write-Host "📝 Analyzing changes..." -ForegroundColor Cyan

# Get git status
$gitStatus = & git -C $ProjectPath status --porcelain

if ([string]::IsNullOrWhiteSpace($gitStatus)) {
    Write-Host "✓ No changes to commit" -ForegroundColor Green
    exit 0
}

# Show what changed
Write-Host ""
Write-Host "Changes detected:" -ForegroundColor Yellow
foreach ($line in $gitStatus) {
    Write-Host "  $line" -ForegroundColor Gray
}

# Stage all changes
Write-Host ""
Write-Host "📦 Staging changes..." -ForegroundColor Cyan
& git -C $ProjectPath add .

# Get diff for AI message generation
$diff = & git -C $ProjectPath diff --cached --shortstat

# Generate smart commit message (using Copilot AI)
# For now, create a simple message template
$commitMessage = "feat: Update project files

These changes include:
$diff"

Write-Host ""
Write-Host "✨ Generated commit message:" -ForegroundColor Green
Write-Host ""
Write-Host $commitMessage -ForegroundColor Yellow
Write-Host ""

# Ask for confirmation
Write-Host "Proceed? [Y/n] " -ForegroundColor Cyan -NoNewline
$confirm = Read-Host

if ($confirm -eq "n") {
    Write-Host "Cancelled" -ForegroundColor Yellow
    & git -C $ProjectPath reset
    exit 0
}

# Create commit
Write-Host "Creating commit..." -ForegroundColor Cyan
& git -C $ProjectPath commit -m $commitMessage

$commitHash = & git -C $ProjectPath rev-parse --short HEAD

Write-Host ""
Write-Host "✅ Commit successful!" -ForegroundColor Green
Write-Host "   Hash: $commitHash" -ForegroundColor Gray
Write-Host ""

# Ask to push
Write-Host "Push to remote? [Y/n] " -ForegroundColor Cyan -NoNewline
$pushConfirm = Read-Host

if ($pushConfirm -ne "n") {
    Write-Host "Pushing..." -ForegroundColor Cyan
    & git -C $ProjectPath push origin HEAD
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ Pushed successfully" -ForegroundColor Green
    } else {
        Write-Host "⚠️  Push failed - keep changes local" -ForegroundColor Yellow
    }
}
