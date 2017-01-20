<script type="text/javascript">
var tt = document.getElementsByTagName("span");
var i;
for (i = 0; i < tt.length; i++){
	var str = String(tt[i].id);
	if(str.slice(0, 4) == "time"){ // "time__<time_t UTC>_1" or "timer_<us>_1"
		var tn = Number(str.slice(6, str.lastIndexOf("_")));
		if(tn > 0){
			var d;
			if(str.charAt(4) == 'r'){
				d = new Date(new Date() - (tn / 1000)); 
			} else {
				d = new Date(tn * 1000);
			}
			tt[i].innerHTML= d.toLocaleDateString() +" "+ d.toLocaleTimeString();
		}
	}
}
</script>