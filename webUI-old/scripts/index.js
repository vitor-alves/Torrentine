new Vue({
	el: '#app',
	data: {
		tableData: []
	},
	mounted: function() {
		this.getTorrent();

		setInterval(function() {
			this.getTorrent();
		}.bind(this), 2000);
	},
	methods: {
		post: function() {
			this.$http.post("http://localhost:9000/", {
				title: 'ronaldo',
				body: 'brilha muito',
				userId: 123
			}).then(function(data) {
				console.log(data);
			});
		},
		getTorrent: function() {
			this.$http.get("http://localhost:9000/torrent")
				.then(response => {
					return response.json();
				})
				.then(data => {
					var arr = [];
					for (var x in data) {
						arr.push(data[x]);
					}
					this.tableData = arr;

				});
		}


	}
});