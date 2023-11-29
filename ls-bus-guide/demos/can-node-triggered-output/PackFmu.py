import urllib.request
import zipfile

from pathlib import Path


# Output FMU path
FMU_PATH = Path('DemoCanNodeTriggeredOutput.fmu')

# Repository to fetch the LS-BUS headers from
LS_BUS_REPO = 'modelica/fmi-ls-bus'
LS_BUS_REV = '473bd5b80730c47373bf41f1c31d44f50de82dd0'
LS_BUS_HEADERS = [ 'fmi3LsBus.h', 'fmi3LsBusCan.h' ]


def main():
    demo_dir = Path(__file__).parent
    root_dir = Path(__file__).parent.parent.parent.parent

    with zipfile.ZipFile(FMU_PATH, 'w') as fmu:
        # Add LS-BUS headers from GitHub repository
        for ls_bus_header in LS_BUS_HEADERS:
            with urllib.request.urlopen(f'https://raw.githubusercontent.com/{LS_BUS_REPO}/{LS_BUS_REV}/headers/{ls_bus_header}') as f:
                fmu.writestr(f'sources/{ls_bus_header}', f.read())

        # Add LS-BUS utility headers
        for file in (demo_dir.parent.parent / 'headers').iterdir():
            fmu.write(file, f'sources/{file.name}')

        # Add source files
        for file in (demo_dir / 'src').iterdir():
            fmu.write(file, f'sources/{file.name}')

        # Add description files
        fmu.write(demo_dir / 'description' / 'modelDescription.xml', 'modelDescription.xml')
        fmu.write(demo_dir / 'description' / 'buildDescription.xml', 'sources/buildDescription.xml')
        fmu.write(demo_dir / 'description' / 'terminalsAndIcons.xml', 'terminalsAndIcons/terminalsAndIcons.xml')
        fmu.write(demo_dir / 'description' / 'fmi-ls-manifest.xml', 'extra/org.fmi-standard.fmi-ls-bus/fmi-ls-manifest.xml')

        # Add additional documentation files
        fmu.write(root_dir / 'LICENSE.txt', 'documentation/licenses/LICENSE.txt')
        fmu.write(demo_dir / 'README.md', 'documentation/README.md')


if __name__ == '__main__':
    main()
