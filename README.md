# OpenFlow 1.3 Software Switch for ns-3 Network Simulator

This is an [OpenFlow 1.3][ofp13] compatible user-space software switch implementation, forked from the original [CPqD OpenFlow 1.3 Software Switch][cpqdofs13] and slightly modified to proper integration with [ns-3 Network Simulator][ns-3]. This code does not modify the original software switch implementation, which is currently maintained in the original repository and regularly synced to the master branch on this one. The modified ns3lib branch includes some callbacks, compiler directives and minor changes in struct declarations to allow integration with the ns-3 simulator.

To compile this software switch for ns-3 integration, check out the code, switch to the ns3lib branch and follow the [original compilation guidelines][compile], but don't forget to include the `--enable-ns3-lib` option during configuration process (`./configure --enable-ns3-lib`). This will create the `libns3ofswitch13.a` static library under udatapath directory. Instructions on how to link the ns-3 simulator to this static library are available at [OpenFlow 1.3 module for ns-3 simulator][ofswitch13].

# Contribute
For contributions to the original OpenFlow 1.3 Software Switch, please refer to the [original project][cpqdofs13]. For ns-3 integration problems, please refer to the [OpenFlow 1.3 module for ns-3 simulator][ofswitch13].

# License
OpenFlow 1.3 Software Switch is released under the BSD license (BSD-like for code from the original Stanford switch).

# Acknowledgments
Thanks to the original [CPqD OpenFlow 1.3 Software Switch][cpqdofs13] contributors. Special thanks to [Eder Leao Fernandes][ederlf], for contributing to the integration between this software switch and the ns-3 simulator.

# Contact
![Email address] (http://www.lrc.ic.unicamp.br/ofswitch13/images/email.gif "Email address")

![Google Analytics](https://ga-beacon.appspot.com/UA-12913294-2/ljerezchaves/ofsoftswitch13?pixel "")

[ofp13]: https://www.opennetworking.org/images/stories/downloads/specification/openflow-spec-v1.3.0.pdf
[cpqdofs13]: https://github.com/CPqD/ofsoftswitch13
[ns-3]: https://www.nsnam.org
[compile]: https://github.com/CPqD/ofsoftswitch13/blob/master/README.md
[ofswitch13]: https://bitbucket.org/ljerezchaves/ofswitch13-module
[ederlf]: https://github.com/ederlf
