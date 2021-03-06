# Configuration
# The name of the source DocBook xml file
INPUT_XML = ../usermanual.xml

# The makefile assumes that you have a 
# directory named images that contains 
# your images. It copies this to the 
# output directory
USER_IMAGES_PARENT_DIR=docsrc

# Name of the desired output directory
# This will be created if it doesn't exist
OUTPUT_DIR = docs

# A list of files to exclude from indexing
INDEXER_EXCLUDED_FILES = ix01.html

# Profiling params. For more information on 
# profiling (conditional text) and DocBook documents, see
# http://www.sagehill.net/docbookxsl/Profiling.html
PROFILE.ARCH = ""
PROFILE.AUDIENCE = ""
PROFILE.CONDITION = ""
PROFILE.CONFORMANCE = ""
PROFILE.LANG = ""
PROFILE.OS = ""
PROFILE.REVISION = ""
PROFILE.REVISIONFLAG = ""
PROFILE.ROLE = ""
PROFILE.SECURITY = ""
PROFILE.STATUS = ""
PROFILE.USERLEVEL = ""
PROFILE.VENDOR = ""
PROFILE.WORDSIZE = ""
PROFILE.ATTRIBUTE = ""
PROFILE.VALUE = ""

# Use this variable to pass in other stringparams
# to the xsltproc pass that generates DocBook output.
# For example:
# OTHER_XSLTPROC_ARGS = --stringparam example.param ""
OTHER_XSLTPROC_ARGS = 

# Path to the DocBook Distribution that 
# contains the xslts etc.
DOCBOOK_DIST := /usr/share/xml/docbook/stylesheet/docbook-xsl/

# =================================================
# You probably don't need to change anything below
# unless you choose to add a validation step.
# ================================================
DOCBOOK_EXTENSIONS_DIR = $(DOCBOOK_DIST)/extensions
INDEXER_JAR   := $(DOCBOOK_EXTENSIONS_DIR)/webhelpindexer.jar
TAGSOUP_JAR   := $(DOCBOOK_EXTENSIONS_DIR)/tagsoup-1.2.1.jar
LUCENE_ANALYZER_JAR   := $(DOCBOOK_EXTENSIONS_DIR)/lucene-analyzers-3.0.0.jar
LUCENE_CORE_JAR   := $(DOCBOOK_EXTENSIONS_DIR)/lucene-core-3.0.0.jar

classpath := $(INDEXER_JAR):$(TAGSOUP_JAR):$(LUCENE_ANALYZER_JAR):$(LUCENE_CORE_JAR)

all: copyfiles docs/index.html # index

${OUTPUT_DIR}/favicon.ico: template/favicon.ico
	cp -p  template/favicon.ico ${OUTPUT_DIR}/favicon.ico
${OUTPUT_DIR}/common/main.js: template/common/main.js
	cp -rp template/common ${OUTPUT_DIR}

copyfiles: ${OUTPUT_DIR}/favicon.ico ${OUTPUT_DIR}/common/main.js

# test ! -d $(USER_IMAGES_PARENT_DIR)/images/ || \
#     cp -rp $(USER_IMAGES_PARENT_DIR)/images ${OUTPUT_DIR}/images

docs/index.html: ${INPUT_XML}
	xsltproc  --xinclude --output xincluded-profiled.xml  \
                  --stringparam  profile.arch ${PROFILE.ARCH} \
                  --stringparam  profile.audience ${PROFILE.AUDIENCE} \
                  --stringparam  profile.condition ${PROFILE.CONDITION} \
                  --stringparam  profile.conformance ${PROFILE.CONFORMANCE} \
                  --stringparam  profile.lang ${PROFILE.LANG} \
                  --stringparam  profile.os ${PROFILE.OS} \
                  --stringparam  profile.revision ${PROFILE.REVISION} \
                  --stringparam  profile.revisionflag ${PROFILE.REVISIONFLAG} \
                  --stringparam  profile.role ${PROFILE.ROLE} \
                  --stringparam  profile.security ${PROFILE.SECURITY} \
                  --stringparam  profile.status ${PROFILE.STATUS} \
                  --stringparam  profile.userlevel ${PROFILE.USERLEVEL} \
                  --stringparam  profile.vendor ${PROFILE.VENDOR} \
                  --stringparam  profile.wordsize ${PROFILE.WORDSIZE} \
                  --stringparam  profile.attribute ${PROFILE.ATTRIBUTE} \
                  --stringparam  profile.value ${PROFILE.VALUE} \
                  ${DOCBOOK_DIST}/profiling/profile.xsl  \
                  ${INPUT_XML}
	xsltproc ${OTHER_XSLTPROC_ARGS} \
                ${DOCBOOK_DIST}/webhelp/xsl/webhelp.xsl \
                xincluded-profiled.xml 
	rm xincluded-profiled.xml

index:
	java \
                -DhtmlDir=$(OUTPUT_DIR) \
                -DindexerLanguage=en \
                -DhtmlExtension=html \
                -DdoStem=true \
                -DindexerExcludedFiles=$(INDEXER_EXCLUDED_FILES) \
                -Dorg.xml.sax.driver=org.ccil.cowan.tagsoup.Parser \
                -Djavax.xml.parsers.SAXParserFactory=org.ccil.cowan.tagsoup.jaxp.SAXFactoryImpl \
                -classpath $(classpath) \
                com.nexwave.nquindexer.IndexerMain

	cp -r template/search/* ${OUTPUT_DIR}/search

clean:
	$(RM) -r ${OUTPUT_DIR}
	mkdir -p $(OUTPUT_DIR)

