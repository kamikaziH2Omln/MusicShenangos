export PYTHON = python

all:
	$(PYTHON) setup.py build

install: .FORCE
	$(PYTHON) setup.py install
	cd docs && $(MAKE) install

check: .FORCE
	cd test && $(PYTHON) test.py

clean: .FORCE
	rm -rfv build
	rm -fv audiotools/*.pyc

distclean: clean
	cd docs && $(MAKE) clean

.FORCE:

construct:
	### This target is no longer required ###

construct_install:
	### This target is no longer required ###


#translation specific targets below

EXECUTABLES = cd2track cd2xmcd coverdump record2track track2cd \
track2track track2xmcd trackcat trackcmp trackinfo tracksplit \
tracklength trackrename tracklint tracktag coverdump editxmcd \
audiotools-config

GLADE_DIR = glade
GLADE_H_FILES = $(GLADE_DIR)/editxmcd.glade.h $(GLADE_DIR)/coverview.glade.h

MODULES = audiotools/__aiff__.py audiotools/__m4a__.py \
audiotools/__ape__.py audiotools/__mp3__.py \
audiotools/__au__.py audiotools/__musepack__.py \
audiotools/cue.py \
audiotools/__flac__.py audiotools/__speex__.py \
audiotools/flac.py audiotools/toc.py \
audiotools/__freedb__.py audiotools/__vorbiscomment__.py \
audiotools/__id3__.py audiotools/__vorbis__.py \
audiotools/__id3v1__.py audiotools/__wavpack__.py \
audiotools/__image__.py audiotools/__wav__.py \
audiotools/__init__.py

audiotools.mo: en_US.po
	msgfmt $< --output-file=$@

en_US.po: audiotools.pot
	msginit --input=$< --locale=en_US --output-file=$@

audiotools.pot: audiotools-cli.pot audiotools-gui.pot
	msgcat -o audiotools.pot audiotools-cli.pot audiotools-gui.pot

audiotools-cli.pot: $(EXECUTABLES) $(MODULES)
	xgettext -L Python --keyword=_ --output=$@ $(EXECUTABLES) $(MODULES)

audiotools-gui.pot: $(GLADE_H_FILES)
	xgettext --keyword=N_ --from-code=UTF-8 --output=$@ $(GLADE_H_FILES)

glade/editxmcd.glade.h: glade/editxmcd.glade
	intltool-extract --type=gettext/glade $<

glade/coverview.glade.h: glade/coverview.glade
	intltool-extract --type=gettext/glade $<
