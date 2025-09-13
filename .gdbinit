python
import sys
import os
sys.path.insert(0, os.path.abspath('.pixi/envs/default/share/gcc-14.3.0/python'))
from libstdcxx.v6.printers import register_libstdcxx_printers
register_libstdcxx_printers(None)
end
