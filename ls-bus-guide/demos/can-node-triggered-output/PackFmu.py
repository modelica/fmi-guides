import urllib.request
import zipfile

from pathlib import Path


# Output FMU path
FMU_PATH = Path('DemoCanNodeTriggeredOutput.fmu')

# Repository to fetch the LS-BUS headers from
LS_BUS_REPO = 'modelica/fmi-ls-bus'
LS_BUS_REV = '6c4b6b27e17046e439a653adafd7e9019925d4bf'
LS_BUS_HEADERS = [ 'fmi3LsBus.h', 'fmi3LsBusCan.h', 'fmi3LsBusUtil.h', 'fmi3LsBusUtilCan.h' ]


def main():
    demo_dir = Path(__file__).parent
    root_dir = Path(__file__).parent.parent.parent.parent

    with zipfile.ZipFile(FMU_PATH, 'w') as fmu:
        # Add LS-BUS headers from GitHub repository
        for ls_bus_header in LS_BUS_HEADERS:
            with urllib.request.urlopen(f'https://raw.githubusercontent.com/{LS_BUS_REPO}/{LS_BUS_REV}/headers/{ls_bus_header}') as f:
                fmu.writestr(f'sources/{ls_bus_header}', f.read())

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
