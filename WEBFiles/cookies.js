function getCookie(name) {
	var prefix = name + "=";
	var cookieStartIndex = document.cookie.indexOf(prefix);
	if (cookieStartIndex == -1)
		return null;
	var cookieEndIndex = document.cookie.indexOf(";", cookieStartIndex
			+ prefix.length);
	if (cookieEndIndex == -1)
		cookieEndIndex = document.cookie.length;
	return unescape(document.cookie.substring(cookieStartIndex + prefix.length,
			cookieEndIndex));
}
function setCookie(name, value) {
	document.cookie = name + "=" + escape(value) + "; path=/";
}
function setCookieElem(name, defv) {
	var val = getCookie(name);
	if (val == null) {
		val = defv;
		setCookie(name, val);
	}
	var f = document.getElementById(name);
	if(f.type === "checkbox") {
		f.checked = val == 1 ? true : false;
	} else if(f.type === "radio") {
		var f = document.getElementsByName(name);
		for(var i=0; i<f.length; i++)
			f[i].checked = f[i].value == val;
	} else {
		f.value = val;
	}
}
function updCookie(name) {
	var val;
	var f = document.getElementById(name);
	if(f.type === "checkbox") {
		val = f.checked ? 1 : 0;
	} else if(f.type === "radio") {
		var f = document.getElementsByName(name);
		for(var i=0; i<f.length; i++)
			if(f[i].checked) { 
				val = f[i].value;
				break;
			}
	} else {
		val = f.value;
	}
	setCookie(name, val);
}
