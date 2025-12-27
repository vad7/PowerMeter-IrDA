<script type="text/javascript">
function updTime() {
	var tt = document.getElementsByTagName("span");
	var i;
	for (i = 0; i < tt.length; i++){
		if(tt[i].id.substr(0, 4) == "time"){ // "time*" = UTC, "timer*" = us
			var n = Number(tt[i].innerHTML);
			if(!isNaN(n)){
				if(tt[i].id[4] == 'r'){
					d = new Date(new Date() - (n / 1000)); 
				} else {
					d = new Date(n * 1000);
				}
				tt[i].innerHTML = n == 0 ? "" : d.toLocaleDateString() +" "+ d.toLocaleTimeString();
			}
		}
	}
}
updTime();
</script>