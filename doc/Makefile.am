
all-local:
	@echo "stylesheet $(PD_DOXML2DB_STY)"
	@for f in *; do \
	    if [ -d $$f ] ; then \
	        TOPDIR=$(abs_top_srcdir) MODULEDIR=$$f $(PD_DOXYGEN) Doxyfile;\
	        doxydir=$$f/doxygen ; \
	        doxmldir=$$doxydir/xml ; \
	        mkdir -p $$doxmldir ; \
	        dxfiles=`cd $$doxmldir; echo *_8c.xml`; \
	        for df in $$dxfiles ; do \
	            module=`echo $$df | sed -e s/_8c.xml//`; \
	            echo "xsltproc $$module"; \
	            $(PD_XSLTPROC) $(PD_DOXML2DB_STY) $$doxmldir/$$df \
	                > $$doxydir/$$f_$$module.xml ; \
	        done ; \
	        rm -rf $$doxmldir; \
	    fi; \
	done
