#!/usr/bin/env python3
"""
generate_csv.py - Automatically generate Template.csv from lights.ino

This script parses structured comments in lights.ino to extract:
- MIDI parameter definitions (@param, @cc, @layer, @tooltip)
- Mode selector definitions (@modes)
- Mode implementations (@mode, @uses)

It validates the metadata and generates Template.csv for FL Studio.

Usage:
    python generate_csv.py                  # Generate Template.csv
    python generate_csv.py --validate       # Validate and compare with existing
"""

import re
import csv
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple
from collections import defaultdict


class LightsInoParser:
    """Parser for extracting metadata from lights.ino"""
    
    def __init__(self, ino_path: Path):
        self.ino_path = ino_path
        self.content = ino_path.read_text(encoding='utf-8')
        
        # Data structures to store parsed information
        self.parameters: Dict[int, Dict[str, str]] = {}  # cc -> {param, layer, tooltip}
        self.mode_selectors: Dict[int, Dict[int, str]] = {}  # cc -> {mode_num: mode_name}
        self.foreground_modes: Dict[str, List[str]] = {}  # mode_name -> [var_names]
        self.background_modes: Dict[str, List[str]] = {}  # mode_name -> [var_names]
        
        # For validation
        self.errors: List[str] = []
        self.warnings: List[str] = []
        
    def parse(self):
        """Parse all metadata from lights.ino"""
        self._parse_parameters()
        self._parse_mode_selectors()
        self._parse_foreground_modes()
        self._parse_background_modes()
        self._validate()
        
    def _parse_parameters(self):
        """Extract parameter metadata blocks"""
        # Regex to match parameter blocks with @layer
        param_pattern = re.compile(
            r'/\*\*\s*\n\s*\*\s*@param\s+([^\n]+)\n'
            r'\s*\*\s*@cc\s+(\d+)\n'
            r'\s*\*\s*@layer\s+(\w+)\n'
            r'\s*\*\s*@tooltip\s+([^\*]+?)\s*\*/',
            re.DOTALL
        )
        
        for match in param_pattern.finditer(self.content):
            param_name = match.group(1).strip()
            cc = int(match.group(2))
            layer = match.group(3).strip()
            tooltip = match.group(4).strip()
            
            if cc in self.parameters:
                self.errors.append(f"Duplicate CC number {cc} for parameter '{param_name}'")
            
            self.parameters[cc] = {
                'param': param_name,
                'layer': layer,
                'tooltip': tooltip
            }
    
    def _parse_mode_selectors(self):
        """Extract mode selector metadata (@modes)"""
        # Regex to match mode selector blocks
        selector_pattern = re.compile(
            r'/\*\*\s*\n\s*\*\s*@param\s+([^\n]+)\n'
            r'\s*\*\s*@cc\s+(\d+)\n'
            r'\s*\*\s*@modes\s+([^\*]+?)\s*\*/',
            re.DOTALL
        )
        
        for match in selector_pattern.finditer(self.content):
            param_name = match.group(1).strip()
            cc = int(match.group(2))
            modes_str = match.group(3).strip()
            
            # Parse the modes string: "0:ModeName0,1:ModeName1,..."
            modes = {}
            for mode_def in modes_str.split(','):
                mode_def = mode_def.strip()
                if ':' in mode_def:
                    mode_num_str, mode_name = mode_def.split(':', 1)
                    mode_num = int(mode_num_str.strip())
                    modes[mode_num] = mode_name.strip()
            
            if cc in self.parameters:
                self.errors.append(f"Duplicate CC number {cc} for mode selector '{param_name}'")
            
            self.parameters[cc] = {
                'param': param_name,
                'layer': '',  # Mode selectors have no layer
                'tooltip': f"Layering of effects",
                'modes': modes
            }
            self.mode_selectors[cc] = modes
    
    def _parse_foreground_modes(self):
        """Extract foreground mode annotations from OnNoteOn and OnControlChange"""
        # Find OnNoteOn function
        onnote_match = re.search(
            r'void\s+OnNoteOn\s*\([^)]*\)\s*\{(.*?)^\}',
            self.content,
            re.DOTALL | re.MULTILINE
        )
        
        # Find OnControlChange function
        oncontrol_match = re.search(
            r'void\s+OnControlChange\s*\([^)]*\)\s*\{(.*?)^\}',
            self.content,
            re.DOTALL | re.MULTILINE
        )
        
        # Combine both functions for foreground mode parsing
        combined_content = ""
        if onnote_match:
            combined_content += onnote_match.group(1)
        if oncontrol_match:
            combined_content += oncontrol_match.group(1)
        
        # Parse mode annotations
        self._parse_mode_annotations(combined_content, self.foreground_modes, 'foreground')
    
    def _parse_background_modes(self):
        """Extract background mode annotations from updateBG"""
        # Find updateBG function
        updatebg_match = re.search(
            r'void\s+updateBG\s*\([^)]*\)\s*\{(.*?)^\}',
            self.content,
            re.DOTALL | re.MULTILINE
        )
        
        if updatebg_match:
            self._parse_mode_annotations(
                updatebg_match.group(1),
                self.background_modes,
                'background'
            )
    
    def _parse_mode_annotations(self, content: str, mode_dict: Dict, mode_type: str):
        """Parse mode annotations from function content"""
        # Regex to match: case N: // @mode ModeName @uses var1,var2,var3
        mode_pattern = re.compile(
            r'case\s+(\d+):\s*//\s*@mode\s+([^@]+?)\s*@uses\s+([\w,]+)',
            re.MULTILINE
        )
        
        seen_cases = set()
        
        for match in mode_pattern.finditer(content):
            case_num = int(match.group(1))
            mode_name = match.group(2).strip()
            vars_str = match.group(3).strip()
            
            # Parse variable list
            var_list = [v.strip() for v in vars_str.split(',')]
            
            if mode_name in mode_dict:
                self.errors.append(
                    f"Duplicate {mode_type} mode '{mode_name}' (case {case_num})"
                )
            
            if case_num in seen_cases:
                self.errors.append(
                    f"Duplicate case {case_num} in {mode_type} modes"
                )
            
            seen_cases.add(case_num)
            mode_dict[mode_name] = var_list
    
    def _validate(self):
        """Validate parsed data"""
        # Check that all CCs 1-15 are defined
        for cc in range(1, 16):
            if cc not in self.parameters:
                self.errors.append(f"Missing parameter definition for CC {cc}")
        
        # Check that mode selectors reference all implemented modes
        if 6 in self.mode_selectors:  # ffMode
            expected_modes = set(self.mode_selectors[6].values())
            actual_modes = set(self.foreground_modes.keys())
            
            missing = expected_modes - actual_modes
            if missing:
                self.errors.append(
                    f"Foreground modes defined but not implemented: {missing}"
                )
            
            extra = actual_modes - expected_modes
            if extra:
                self.errors.append(
                    f"Foreground modes implemented but not in @modes list: {extra}"
                )
        
        if 9 in self.mode_selectors:  # bgMode
            expected_modes = set(self.mode_selectors[9].values())
            actual_modes = set(self.background_modes.keys())
            
            missing = expected_modes - actual_modes
            if missing:
                self.errors.append(
                    f"Background modes defined but not implemented: {missing}"
                )
            
            extra = actual_modes - expected_modes
            if extra:
                self.errors.append(
                    f"Background modes implemented but not in @modes list: {extra}"
                )
    
    def get_variable_to_modes_mapping(self) -> Dict[str, List[str]]:
        """Build mapping from variable name to list of modes that use it"""
        var_to_modes = defaultdict(list)
        
        # Add foreground modes
        for mode_name, var_list in self.foreground_modes.items():
            for var in var_list:
                var_to_modes[var].append(mode_name)
        
        # Add background modes
        for mode_name, var_list in self.background_modes.items():
            for var in var_list:
                var_to_modes[var].append(mode_name)
        
        return dict(var_to_modes)


class CSVGenerator:
    """Generate Template.csv from parsed metadata"""
    
    def __init__(self, parser: LightsInoParser):
        self.parser = parser
    
    def generate(self, output_path: Path):
        """Generate CSV file"""
        rows = []
        
        # Header row
        rows.append([
            'Parameter', 'CC', 'Minimum Value', 'Maximum Value',
            'Layer', 'Tooltip', 'Choices'
        ])
        
        # Get variable to modes mapping
        var_to_modes = self.parser.get_variable_to_modes_mapping()
        
        # Variable name to CC mapping (for finding which CC uses which variable)
        var_name_to_cc = self._build_var_name_mapping()
        
        # Generate rows for each CC
        for cc in range(1, 16):
            if cc not in self.parser.parameters:
                continue
            
            param_info = self.parser.parameters[cc]
            param_name = param_info['param']
            layer = param_info['layer']
            tooltip = param_info['tooltip']
            
            # Choices column
            if 'modes' in param_info:
                # Mode selector - list all available modes
                modes = param_info['modes']
                choices = '\n'.join([modes[i] for i in sorted(modes.keys())])
            else:
                # Regular parameter - list modes that use the variable
                var_name = self._cc_to_var_name(cc)
                if var_name and var_name in var_to_modes:
                    choices = '\n'.join(var_to_modes[var_name])
                else:
                    choices = ''
            
            rows.append([
                param_name,
                str(cc),
                '0',
                '127',
                layer,
                tooltip,
                choices
            ])
        
        # Write CSV
        with output_path.open('w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerows(rows)
    
    def _build_var_name_mapping(self) -> Dict[str, int]:
        """Build mapping from variable name to CC number"""
        # Hardcoded mapping based on OnControlChange switch statement
        return {
            'ffHue': 1, 'ffSat': 2, 'ffBright': 3, 'ffLedStart': 4, 'ffLedLength': 5,
            'ffMode': 6, 'lines': 7, 'cAmp': 8, 'bgMode': 9, 'pan': 10,
            'bgHue': 11, 'bgSat': 12, 'bgBright': 13, 'bgLedStart': 14, 'bgLedLength': 15
        }
    
    def _cc_to_var_name(self, cc: int) -> str:
        """Get variable name for a given CC number"""
        var_map = self._build_var_name_mapping()
        for var_name, var_cc in var_map.items():
            if var_cc == cc:
                return var_name
        return ''


def compare_csvs(generated_path: Path, existing_path: Path) -> bool:
    """Compare generated CSV with existing CSV"""
    if not existing_path.exists():
        print(f"Warning: Existing CSV not found at {existing_path}")
        return False
    
    with generated_path.open('r', encoding='utf-8') as f1, \
         existing_path.open('r', encoding='utf-8') as f2:
        
        gen_lines = f1.readlines()
        exist_lines = f2.readlines()
        
        if gen_lines == exist_lines:
            print("✓ Generated CSV matches existing Template.csv")
            return True
        else:
            print("✗ Generated CSV differs from existing Template.csv")
            print("\nDifferences:")
            
            max_lines = max(len(gen_lines), len(exist_lines))
            for i in range(max_lines):
                gen_line = gen_lines[i].rstrip() if i < len(gen_lines) else '<missing>'
                exist_line = exist_lines[i].rstrip() if i < len(exist_lines) else '<missing>'
                
                if gen_line != exist_line:
                    print(f"\nLine {i+1}:")
                    print(f"  Generated: {gen_line}")
                    print(f"  Existing:  {exist_line}")
            
            return False


def main():
    """Main entry point"""
    validate_only = '--validate' in sys.argv
    
    # Paths
    script_dir = Path(__file__).parent
    ino_path = script_dir / 'Source' / 'lights' / 'lights.ino'
    csv_path = script_dir / 'Template.csv'
    temp_csv_path = script_dir / 'Template_generated.csv'
    
    if not ino_path.exists():
        print(f"Error: {ino_path} not found")
        sys.exit(1)
    
    # Parse lights.ino
    print(f"Parsing {ino_path}...")
    parser = LightsInoParser(ino_path)
    parser.parse()
    
    # Report errors and warnings
    if parser.errors:
        print("\n❌ ERRORS:")
        for error in parser.errors:
            print(f"  - {error}")
        sys.exit(1)
    
    if parser.warnings:
        print("\n⚠ WARNINGS:")
        for warning in parser.warnings:
            print(f"  - {warning}")
    
    print(f"✓ Found {len(parser.parameters)} parameters")
    print(f"✓ Found {len(parser.foreground_modes)} foreground modes")
    print(f"✓ Found {len(parser.background_modes)} background modes")
    
    # Generate CSV
    generator = CSVGenerator(parser)
    
    if validate_only:
        print(f"\nGenerating temporary CSV for validation...")
        generator.generate(temp_csv_path)
        matches = compare_csvs(temp_csv_path, csv_path)
        temp_csv_path.unlink()  # Clean up
        sys.exit(0 if matches else 1)
    else:
        print(f"\nGenerating {csv_path}...")
        generator.generate(csv_path)
        print(f"✓ Successfully generated {csv_path}")


if __name__ == '__main__':
    main()
