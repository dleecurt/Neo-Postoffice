all:
	@./config/softlinks
	@./config/configure
	@./config/buildall.sh all

srcdist:
	@./config/srcdist.sh

clean:
	@./config/buildall.sh clean

bindist:
	@./config/buildall.sh bindist

cleanall:
	rm -rf `find . -name "arch" -print`
	rm -rf vroot/*
	rm -f config/config.*

update:
	@./config/update
