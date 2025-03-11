#!/bin/bash

# Color constants
LIGHT_GREEN='\033[1;32m'
LIGHT_RED='\033[1;31m'
CLEAR='\033[0m'

# Function to log messages with [INFO]: prefix
log_info() {
    echo -e "[INFO]: $1"
}

# Function to log success messages with light green color
log_success() {
    echo -e "${LIGHT_GREEN}[INFO]: $1${CLEAR}"
}

# Function to log error messages with light red color
log_error() {
    echo -e "${LIGHT_RED}[INFO]: $1${CLEAR}"
}

# Check if glslc is installed
if ! command -v glslc >/dev/null 2>&1; then
    log_error "Error: 'glslc' not found. Please ensure it is installed and available in your PATH."
    exit 1
else
    log_success "'glslc' found successfully."
fi

# Arrays to hold .frag and .vert files
frag_files=()
vert_files=()

search_dir="../engine/shaders/**"
output_dir="../engine/shaders/compiled"

log_info "Search directory: $search_dir"

# Check if the compiled directory exists, if not, create it
if [[ ! -d $output_dir ]]; then
    log_info "Output directory does not exist. Creating $output_dir..."
    mkdir -p "$output_dir"
    if [[ $? -eq 0 ]]; then
        log_success "Successfully created $output_dir."
    else
        log_error "Failed to create $output_dir."
        exit 1
    fi
else
    log_success "Output directory $output_dir successfully found."
fi

# Find all .frag and .vert files in the search directory.
for file in $search_dir; do
    if [[ -f $file ]]; then
        case $file in
            *.frag) 
                frag_files+=("$file")
                log_info "Found fragment shader: $file"
                ;;
            *.vert) 
                vert_files+=("$file")
                log_info "Found vertex shader: $file"
                ;;
        esac
    fi
done

log_info "Found (${#frag_files[@]}) fragment shaders and (${#vert_files[@]}) vertex shaders."

# Compile .frag files.
for frag in "${frag_files[@]}"; do
    filename=$(basename -- "$frag")
    output="${filename%.*}_frag.spv"
    log_info "Compiling fragment shader $frag to $output_dir/$output..."
    glslc "$frag" -o "$output_dir/$output"
    if [[ $? -eq 0 ]]; then
        log_success "Successfully compiled $frag to $output_dir/$output."
    else
        log_error "Failed to compile $frag."
    fi
done

# Compile .vert files.
for vert in "${vert_files[@]}"; do
    filename=$(basename -- "$vert")
    output="${filename%.*}_vert.spv"
    log_info "Compiling vertex shader $vert to $output_dir/$output"
    glslc "$vert" -o "$output_dir/$output"
    if [[ $? -eq 0 ]]; then
        log_success "Successfully compiled $vert to $output_dir/$output."
    else
        log_error "Failed to compile $vert."
    fi
done

log_info "Shader compilation completed."