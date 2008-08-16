
all-local:
	@for f in *; do \
	    if [ -d $$f ] ; then \
	        echo "===== building doc's for '$$f' module ====="; \
	        pushd $$f/lyx ; \
	        lyxfiles=`echo *.lyx` ;\
		for lf in $$lyxfiles ; do \
	            xf=$(sunst %.lyx, %.xml, $$lf); \
	            echo "export $$lf"; \
	            rm -f $$xf; \
	            $(PD_LYX) --export docbook-xml $$lf ; \
	        done ; \
	        popd ;\
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
