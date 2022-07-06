"use strict";(self.webpackChunkwebsite=self.webpackChunkwebsite||[]).push([[6189],{3905:function(e,t,n){n.d(t,{Zo:function(){return p},kt:function(){return f}});var r=n(7294);function a(e,t,n){return t in e?Object.defineProperty(e,t,{value:n,enumerable:!0,configurable:!0,writable:!0}):e[t]=n,e}function i(e,t){var n=Object.keys(e);if(Object.getOwnPropertySymbols){var r=Object.getOwnPropertySymbols(e);t&&(r=r.filter((function(t){return Object.getOwnPropertyDescriptor(e,t).enumerable}))),n.push.apply(n,r)}return n}function o(e){for(var t=1;t<arguments.length;t++){var n=null!=arguments[t]?arguments[t]:{};t%2?i(Object(n),!0).forEach((function(t){a(e,t,n[t])})):Object.getOwnPropertyDescriptors?Object.defineProperties(e,Object.getOwnPropertyDescriptors(n)):i(Object(n)).forEach((function(t){Object.defineProperty(e,t,Object.getOwnPropertyDescriptor(n,t))}))}return e}function l(e,t){if(null==e)return{};var n,r,a=function(e,t){if(null==e)return{};var n,r,a={},i=Object.keys(e);for(r=0;r<i.length;r++)n=i[r],t.indexOf(n)>=0||(a[n]=e[n]);return a}(e,t);if(Object.getOwnPropertySymbols){var i=Object.getOwnPropertySymbols(e);for(r=0;r<i.length;r++)n=i[r],t.indexOf(n)>=0||Object.prototype.propertyIsEnumerable.call(e,n)&&(a[n]=e[n])}return a}var s=r.createContext({}),c=function(e){var t=r.useContext(s),n=t;return e&&(n="function"==typeof e?e(t):o(o({},t),e)),n},p=function(e){var t=c(e.components);return r.createElement(s.Provider,{value:t},e.children)},u={inlineCode:"code",wrapper:function(e){var t=e.children;return r.createElement(r.Fragment,{},t)}},d=r.forwardRef((function(e,t){var n=e.components,a=e.mdxType,i=e.originalType,s=e.parentName,p=l(e,["components","mdxType","originalType","parentName"]),d=c(n),f=a,m=d["".concat(s,".").concat(f)]||d[f]||u[f]||i;return n?r.createElement(m,o(o({ref:t},p),{},{components:n})):r.createElement(m,o({ref:t},p))}));function f(e,t){var n=arguments,a=t&&t.mdxType;if("string"==typeof e||a){var i=n.length,o=new Array(i);o[0]=d;var l={};for(var s in t)hasOwnProperty.call(t,s)&&(l[s]=t[s]);l.originalType=e,l.mdxType="string"==typeof e?e:a,o[1]=l;for(var c=2;c<i;c++)o[c]=n[c];return r.createElement.apply(null,o)}return r.createElement.apply(null,n)}d.displayName="MDXCreateElement"},9150:function(e,t,n){n.r(t),n.d(t,{assets:function(){return p},contentTitle:function(){return s},default:function(){return f},frontMatter:function(){return l},metadata:function(){return c},toc:function(){return u}});var r=n(3117),a=n(102),i=(n(7294),n(3905)),o=["components"],l={},s="Security",c={unversionedId:"developer/Security",id:"developer/Security",title:"Security",description:"Wireless Security",source:"@site/../docs/developer/Security.md",sourceDirName:"developer",slug:"/developer/Security",permalink:"/docs/developer/Security",draft:!1,editUrl:"https://github.com/terragraph/meta-terragraph/edit/main/docs/../docs/developer/Security.md",tags:[],version:"current",frontMatter:{},sidebar:"developerManualSidebar",previous:{title:"High Availability",permalink:"/docs/developer/High_Availability"},next:{title:"Release Conventions",permalink:"/docs/developer/Release_Conventions"}},p={},u=[{value:"Wireless Security",id:"wireless-security",level:2},{value:"Wired Security",id:"wired-security",level:2},{value:"Authentication Server",id:"authentication-server",level:3},{value:"Authenticator",id:"authenticator",level:3},{value:"Supplicant",id:"supplicant",level:3},{value:"Firewall Configuration",id:"firewall-configuration",level:2},{value:"Configuration Options",id:"configuration-options",level:3}],d={toc:u};function f(e){var t=e.components,n=(0,a.Z)(e,o);return(0,i.kt)("wrapper",(0,r.Z)({},d,n,{components:t,mdxType:"MDXLayout"}),(0,i.kt)("h1",{id:"security"},"Security"),(0,i.kt)("h2",{id:"wireless-security"},"Wireless Security"),(0,i.kt)("p",null,"Terragraph links support WPA-PSK and IEEE 802.1X for security. Refer to\n",(0,i.kt)("a",{parentName:"p",href:"/docs/developer/Network_Ignition#network-ignition-link-layer-security"},"Link-Layer Security"),"\nfor more details."),(0,i.kt)("a",{id:"security-wired-security"}),(0,i.kt)("h2",{id:"wired-security"},"Wired Security"),(0,i.kt)("p",null,"Terragraph provides CPE interface security using the IEEE 802.1X standard.\nSecurity can be enabled on each CPE interface independently by setting\n",(0,i.kt)("inlineCode",{parentName:"p"},"wiredSecurityEnable")," in the node configuration, as well as ",(0,i.kt)("inlineCode",{parentName:"p"},"eapolParams"),", for\nexample:"),(0,i.kt)("pre",null,(0,i.kt)("code",{parentName:"pre",className:"language-json"},'{\n  "cpeConfig": {\n    "TenGigabitEthernet0": {\n      "wiredSecurityEnable": true\n    }\n  },\n  "eapolParams": {\n    "ca_cert_path": "/data/secure/keys/ca.pem",\n    "client_cert_path": "/data/secure/keys/client.pem",\n    "private_key_path": "/data/secure/keys/client.key",\n    "radius_server_ip": "1234:5678:9abc::def",\n    "radius_server_port": 1812,\n    "radius_user_identity": "some-user",\n    "secrets": {\n      "private_key_password": "some-passphrase",\n      "radius_server_shared_secret": "some-secret",\n      "radius_user_password": "some-password"\n    }\n  }\n}\n')),(0,i.kt)("p",null,"When enabled, the CPE interface will only allow EAPoL frames to pass, and will\ndrop all other packets until successful authentication. This EAPoL-only\nforwarding is only supported in VPP mode, and is implemented in the ",(0,i.kt)("inlineCode",{parentName:"p"},"vpp-tgcfg"),'\nplugin via the "wired security" interface configuration option.'),(0,i.kt)("p",null,"802.1X authentication involves three parties: an authentication server, an\nauthenticator, and a supplicant."),(0,i.kt)("p",{align:"center"},(0,i.kt)("img",{src:"/figures/802.1X.png",width:"512"})),(0,i.kt)("h3",{id:"authentication-server"},"Authentication Server"),(0,i.kt)("p",null,"IEEE 802.1X requires an authentication server that can tell the authenticator\nif a connection is to be allowed. Terragraph expects an authentication server\nthat supports the RADIUS and EAP protocols. The authentication server must be\nreachable by the authenticator (CN or DN with CPE interface enabled)."),(0,i.kt)("h3",{id:"authenticator"},"Authenticator"),(0,i.kt)("p",null,"When wired security is enabled on a CPE interface, the CPE interface takes the\nrole of the authenticator. The Terragraph node is responsible for managing one\nLinux authenticator process (",(0,i.kt)("inlineCode",{parentName:"p"},"hostapd"),") for each CPE interface with wired\nsecurity enabled, which is configured and launched by the\n",(0,i.kt)("inlineCode",{parentName:"p"},"start_cpe_security.sh")," script. Logs for these processes are written to\n",(0,i.kt)("inlineCode",{parentName:"p"},"/tmp/hostapd_<iface>"),". The authenticator will be restarted upon CPE interface\nstate changes. So, if a CPE device is removed or replaced, it is strongly\nrecommended to shut down CPE interface before adding the new device (currently,\nTerragraph isn't able to automatically detect connection change and\nre-authenticate the same CPE interface)."),(0,i.kt)("h3",{id:"supplicant"},"Supplicant"),(0,i.kt)("p",null,"The supplicant refers to the CPE device connecting over the Terragraph CPE\ninterface and sending traffic through the Terragraph network. It is expected\nthat each CPE device has pre-provisioned certificates and runs a Linux\nsupplicant process (",(0,i.kt)("inlineCode",{parentName:"p"},"wpa_supplicant"),") on the interface that connects to the\nTerragraph node."),(0,i.kt)("h2",{id:"firewall-configuration"},"Firewall Configuration"),(0,i.kt)("p",null,"Terragraph nodes are able to apply ",(0,i.kt)("inlineCode",{parentName:"p"},"ip6tables")," firewall rules via node\nconfiguration and the ",(0,i.kt)("inlineCode",{parentName:"p"},"/usr/bin/update_firewall")," script. This does not support\nfiltering of throughput traffic, but can protect traffic destined to Linux\nitself. More specifically:"),(0,i.kt)("ul",null,(0,i.kt)("li",{parentName:"ul"},"The rules only modify the ",(0,i.kt)("inlineCode",{parentName:"li"},"INPUT")," table."),(0,i.kt)("li",{parentName:"ul"},"The ",(0,i.kt)("inlineCode",{parentName:"li"},"FORWARD")," table is always left to ",(0,i.kt)("inlineCode",{parentName:"li"},"ACCEPT")," all."),(0,i.kt)("li",{parentName:"ul"},"The ",(0,i.kt)("inlineCode",{parentName:"li"},"OUTPUT")," table is always left to ",(0,i.kt)("inlineCode",{parentName:"li"},"ACCEPT")," all.")),(0,i.kt)("h3",{id:"configuration-options"},"Configuration Options"),(0,i.kt)("p",null,"Terragraph's simple ",(0,i.kt)("inlineCode",{parentName:"p"},"ip6tables")," implementation allows the following options\nunder the ",(0,i.kt)("inlineCode",{parentName:"p"},"firewallConfig")," node configuration structure:"),(0,i.kt)("ul",null,(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"allowEstalished"),": Allow already-established connections ",(0,i.kt)("em",{parentName:"li"},"(recommended)"),"."),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"allowICMPv6"),": Allow all ICMPv6 traffic ",(0,i.kt)("em",{parentName:"li"},"(recommended)"),"."),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"allowLinkLocal"),": Allow any packets from source prefix ",(0,i.kt)("inlineCode",{parentName:"li"},"fe80::/10")," (important\nfor Open/R's Spark module)."),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"allowLoopback"),": Allow anything destined to the ",(0,i.kt)("inlineCode",{parentName:"li"},"lo")," interface. This is\n",(0,i.kt)("strong",{parentName:"li"},"NOT")," recommended, as it effectively allows ",(0,i.kt)("em",{parentName:"li"},"anything")," to the management\naddress, but could be handy for debugging if firewall issues are suspected."),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"defaultPolicy"),": Set the table to ",(0,i.kt)("inlineCode",{parentName:"li"},"ACCEPT")," or ",(0,i.kt)("inlineCode",{parentName:"li"},"DROP")," by default (default is\n",(0,i.kt)("inlineCode",{parentName:"li"},"ACCEPT"),")."),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"tcpPorts"),': TCP ports to open from any address. This is a comma-separated\nlist, e.g. "22,179". It is ',(0,i.kt)("em",{parentName:"li"},"recommended")," to always keep 22 (SSH) and 179 (BGP)\nopen."),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"udpPorts"),": UDP ports to open from any address. This is a comma-separated\nlist. It is ",(0,i.kt)("em",{parentName:"li"},"recommended")," to always keep 123 (NTP) open.")))}f.isMDXComponent=!0}}]);