param(
    [string]$KnowledgeRoot = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "knowledge"),
    [string]$ReportPath = ""
)

$ErrorActionPreference = "Stop"

function Add-Finding {
    param(
        [System.Collections.Generic.List[object]]$List,
        [string]$Code,
        [string]$Message
    )

    $List.Add([pscustomobject]@{
        code = $Code
        message = $Message
    }) | Out-Null
}

function Read-JsonFile {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing JSON file: $Path"
    }

    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

$taxonomyPath = Join-Path $KnowledgeRoot "COOKBOOK_TAXONOMY.json"
$cardsPath = Join-Path $KnowledgeRoot "cards\electronics_cookbook_seed.jsonl"
$taxonomy = Read-JsonFile $taxonomyPath

$requiredFields = @(
    "category",
    "subcategory",
    "purpose",
    "whenToUse",
    "whenNotToUse",
    "requiredComponents",
    "designProcedure",
    "toolRecipe",
    "analysisRecipe",
    "validationCriteria",
    "failureModes",
    "iterationRules",
    "capabilityGaps",
    "provenance",
    "text"
)

$recommendedFields = @(
    "parameters",
    "relatedEntries"
)

$taxonomySet = @{}
foreach ($category in $taxonomy.categories) {
    if (-not [string]::IsNullOrWhiteSpace($category)) {
        $taxonomySet[$category] = $true
    }
}

$errors = [System.Collections.Generic.List[object]]::new()
$warnings = [System.Collections.Generic.List[object]]::new()
$ids = @{}
$entries = @()
$lineNo = 0

if (-not (Test-Path -LiteralPath $cardsPath -PathType Leaf)) {
    throw "Missing cookbook JSONL: $cardsPath"
}

foreach ($line in Get-Content -LiteralPath $cardsPath) {
    $lineNo++
    if ([string]::IsNullOrWhiteSpace($line)) {
        continue
    }

    try {
        $card = $line | ConvertFrom-Json
    } catch {
        Add-Finding $errors "jsonl.parse" "Line $lineNo is not valid JSON: $($_.Exception.Message)"
        continue
    }

    $entries += $card
    $id = [string]$card.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        $id = "<line $lineNo>"
        Add-Finding $errors "card.id.missing" "Line $lineNo is missing id."
    }

    if ($ids.ContainsKey($id)) {
        Add-Finding $errors "card.id.duplicate" "$id is duplicated."
    } else {
        $ids[$id] = $true
    }

    $category = [string]$card.category
    if ([string]::IsNullOrWhiteSpace($category)) {
        Add-Finding $errors "card.category.missing" "$id is missing category."
    } elseif (-not $taxonomySet.ContainsKey($category)) {
        Add-Finding $errors "card.category.unknown" "$id category is not in taxonomy: $category"
    }

    foreach ($field in $requiredFields) {
        $property = $card.PSObject.Properties[$field]
        $missing = $null -eq $property
        if (-not $missing) {
            $value = $property.Value
            if ($null -eq $value) {
                $missing = $true
            } elseif ($value -is [string] -and [string]::IsNullOrWhiteSpace($value)) {
                $missing = $true
            } elseif ($value -is [array] -and $value.Count -eq 0) {
                $missing = $true
            }
        }

        if ($missing) {
            Add-Finding $errors "card.field.required" "$id is missing required field '$field'."
        }
    }

    foreach ($field in $recommendedFields) {
        if ($null -eq $card.PSObject.Properties[$field]) {
            Add-Finding $warnings "card.field.recommended" "$id is missing recommended field '$field'."
        }
    }

    if (([string]$card.text).Length -lt 120) {
        Add-Finding $warnings "card.text.short" "$id summary text is short for retrieval."
    }
}

$coveredSet = @{}
foreach ($entry in $entries) {
    $category = [string]$entry.category
    if ($taxonomySet.ContainsKey($category)) {
        $coveredSet[$category] = $true
    }
}

$missingCategories = @()
foreach ($category in $taxonomy.categories) {
    if (-not $coveredSet.ContainsKey($category)) {
        $missingCategories += $category
    }
}

foreach ($category in $missingCategories) {
    Add-Finding $errors "coverage.category.missing" "No cookbook entry covers taxonomy category: $category"
}

$coveredCount = $taxonomy.categories.Count - $missingCategories.Count
$coverageRatio = if ($taxonomy.categories.Count -eq 0) { 0 } else { $coveredCount / $taxonomy.categories.Count }
$status = if ($errors.Count -eq 0) { "passed" } else { "failed" }

$report = [pscustomobject]@{
    schemaVersion = 1
    kind = "djehuti_cookbook_validation_report"
    status = $status
    knowledgeRoot = $KnowledgeRoot
    taxonomyFile = $taxonomyPath
    cookbookFile = $cardsPath
    cookbookEntryCount = $entries.Count
    requiredCategoryCount = $taxonomy.categories.Count
    coveredCategoryCount = $coveredCount
    missingCategoryCount = $missingCategories.Count
    coverageRatio = [math]::Round($coverageRatio, 6)
    missingCategories = $missingCategories
    errorCount = $errors.Count
    warningCount = $warnings.Count
    errors = @($errors)
    warnings = @($warnings)
}

if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    $reportDirectory = Split-Path -Parent $ReportPath
    if (-not [string]::IsNullOrWhiteSpace($reportDirectory)) {
        New-Item -ItemType Directory -Force -Path $reportDirectory | Out-Null
    }
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
}

$report | ConvertTo-Json -Depth 8

if ($errors.Count -ne 0) {
    exit 1
}
