# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'NXP Linux kernel Summer School'
copyright = '2026, NXP'
author = 'NXP'

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

numfig = True

# same formatting you'd see in LATEX
numfig_format = {
	'figure': 'Figure %s',
	'table': 'Table %s',
}

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'sphinx_rtd_theme'
html_theme_options = {
	'logo_only': True
}

html_static_path = ['_static']

html_css_files = ['css/custom.css']

html_show_sphinx = False

html_logo = '_static/LOGO.svg'
