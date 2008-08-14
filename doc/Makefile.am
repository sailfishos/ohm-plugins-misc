
all-local:
	@for f in *; do \
	    if [ -d $$f ] ; then \
	        TOPDIR=$(abs_top_srcdir) MODULEDIR=$$f $(DX_DOXYGEN) Doxyfile;\
	        doxydir=$$f/doxygen ; \
	        doxmldir=$$doxydir/xml ; \
	        mkdir -p $$doxmldir ; \
	        dxfiles=`cd $$doxmldir; echo *_8c.xml`; \
	        for df in $$dxfiles ; do \
	            module=`echo $$df | sed -e s/_8c.xml//`; \
	            echo "xsltproc $$module"; \
	            /usr/bin/xsltproc /usr/share/policy/doc-tools/doxygen/dbdoxml.sty $$doxmldir/$$df > $$doxydir/$$f_$$module.xml ; \
	        done ; \
	        rm -rf $$doxmldir; \
	    fi; \
	done
