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
				this.name = data['id_torrent'].name;
				this.down_speed = data['id_torrent'].down_rate/1000 + 'Kb/s';
				this.up_speed = data['id_torrent'].up_rate/1000 + 'Kb/s';
			});
		}
	}
});