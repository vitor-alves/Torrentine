new Vue({
	el: '#app',
	data: {
		name: '',
		down_speed: '',
		up_speed: ''
	},
	methods: {
		post: function() {
			this.$http.post("http://localhost:9000/", 
				{
					title: 'oi',
					body: 'ui',
					userId: 123
				}).then(function(data) {
					console.log(data);
				});
		},
		get: function() {
			this.$http.get("http://localhost:9000/torrent")
			.then(response => {
				return response.json();
			})
			.then(data => {
				this.name = data['56a21a042238079f5cc68a81af8f3689dd923b0b'].name;
				this.down_speed = data['56a21a042238079f5cc68a81af8f3689dd923b0b'].down_rate/1000 + 'Kb/s';
				this.up_speed = data['56a21a042238079f5cc68a81af8f3689dd923b0b'].up_rate/1000 + 'Kb/s';
			});
		}
	}
});