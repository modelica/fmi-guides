import urllib.request
import zipfile

from pathlib import Path


# List of FMUs to be created with FMU-specifix directory and output file
FMUS = [
    ('node1', 'DemoFlexRayNode1.fmu'),
    ('node2', 'DemoFlexRayNode2.fmu')
]

# Repository to fetch the LS-BUS headers from
LS_BUS_REPO = 'modelica/fmi-ls-bus'
LS_BUS_REV = 'v1.1.0-alpha.1'
LS_BUS_HEADERS = [ 'fmi3LsBus.h', 'fmi3LsBusFlexRay.h', 'fmi3LsBusUtil.h', 'fmi3LsBusUtilFlexRay.h' ]


def main():
    demo_dir = Path(__file__).parent
    root_dir = Path(__file__).parent.parent.parent.parent

    for fmu_dir_name, fmu_file_name in FMUS:
        fmu_dir = demo_dir / fmu_dir_name
        with zipfile.ZipFile(fmu_file_name, 'w') as fmu:
            # Add LS-BUS headers from GitHub repository
            for ls_bus_header in LS_BUS_HEADERS:
                with urllib.request.urlopen(f'https://raw.githubusercontent.com/{LS_BUS_REPO}/{LS_BUS_REV}/headers/{ls_bus_header}') as f:
                    fmu.writestr(f'sources/{ls_bus_header}', f.read())

            # Add source files
            for file in (demo_dir / 'src').iterdir():
                fmu.write(file, f'sources/{file.name}')
            fmu.write(fmu_dir / 'src' / 'FmuIdentifier.h', 'sources/FmuIdentifier.h')

            # Add description files
            fmu.write(fmu_dir / 'description' /  'modelDescription.xml', 'modelDescription.xml')
            fmu.write(fmu_dir / 'description' / 'buildDescription.xml', 'sources/buildDescription.xml')
            fmu.write(demo_dir / 'description' / 'terminalsAndIcons.xml', 'terminalsAndIcons/terminalsAndIcons.xml')
            fmu.write(demo_dir / 'description' / 'fmi-ls-manifest.xml', 'extra/org.fmi-standard.fmi-ls-bus/fmi-ls-manifest.xml')

            # Add additional documentation files
            fmu.write(root_dir / 'LICENSE.txt', 'documentation/licenses/LICENSE.txt')
            fmu.write(demo_dir / 'README.md', 'documentation/README.md')


if __name__ == '__main__':
    main()
